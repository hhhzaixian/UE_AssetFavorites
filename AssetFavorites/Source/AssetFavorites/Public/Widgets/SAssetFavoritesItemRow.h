// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "AssetFavoritesTypes.h"
#include "AssetRegistry/AssetData.h"

class SInlineEditableTextBlock;
class FAssetThumbnail;
class FAssetThumbnailPool;
class SVerticalBox;

/**
 * 收藏项行 Widget - 多列版本
 */
class SAssetFavoritesItemRow : public SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>
{
public:
	SLATE_BEGIN_ARGS(SAssetFavoritesItemRow) 
		: _ThumbnailPool()
		, _OwnerTreeView()
	{}
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_ARGUMENT(TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>>, OwnerTreeView)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FFavoritesTreeNode> InNode);

	// SMultiColumnTableRow 接口
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	// SWidget 接口
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** 获取资产图标 */
	const FSlateBrush* GetAssetIcon() const;

	/** 获取文件夹图标颜色 */
	FSlateColor GetFolderIconColor() const;

	/** 获取版本控制状态图标 */
	const FSlateBrush* GetSourceControlIcon() const;

	/** 获取资产类型显示名称 */
	FText GetAssetTypeName() const;

	/** 获取显示名称 */
	FText GetDisplayNameText() const;

	/** 检查资产包是否标脏 */
	bool IsPackageDirty() const;

	/** 获取标脏图标的可见性 */
	EVisibility GetDirtyBadgeVisibility() const;

	/** 显示右键菜单 */
	void ShowContextMenu(const FGeometry& Geometry, const FPointerEvent& MouseEvent);

	/** 右键菜单操作 */
	void OnOpenAsset();
	void OnLocateInContentBrowser();
	void OnEditNote();
	void OnRemoveNote();
	void OnDeleteItem();

	/** 复制路径操作（对齐 UE Content Browser 的 4 个 Copy 菜单项，支持多选） */
	void OnCopyReference();   // Copy Reference     -> Class'/Game/Path/Asset.Asset'
	void OnCopyObjectPath();  // Copy Object Path   -> /Game/Path/Asset.Asset
	void OnCopyPackagePath(); // Copy Package Path  -> /Game/Path/Asset
	void OnCopyFilePath();    // Copy File Path     -> 磁盘 .uasset 全路径

	/** Copy 路径格式枚举 */
	enum class ECopyPathFormat : uint8
	{
		Reference,
		ObjectPath,
		PackagePath,
		FilePath,
	};

	/** 收集需要复制的目标节点：若 TreeView 内当前选中多项，则返回全部选中的 Item 节点；
	 *  否则仅返回当前行节点。结果只包含 Item 节点（跳过 Category）。 */
	TArray<TSharedPtr<FFavoritesTreeNode>> CollectCopyTargetNodes() const;

	/** 把单个节点按指定格式格式化为剪贴板文本；不适用的组合返回空串。 */
	static FString FormatPathForNode(const TSharedPtr<FFavoritesTreeNode>& InNode, ECopyPathFormat Format);

	/** 多选复制公共逻辑：收集 -> 格式化 -> 用 \n 拼接 -> 写剪贴板。 */
	void CopySelectedPaths(ECopyPathFormat Format) const;

	/** 按钮点击 */
	FReply OnLocateClicked();
	FReply OnDeleteClicked();

public:
	/** 进入备注编辑模式（供外部调用） */
	void EnterNoteEditingMode();

	/** 进入名称编辑模式（供外部调用） */
	void EnterNameEditingMode();

	/** 获取缓存的 Item ID */
	const FGuid& GetCachedItemId() const { return CachedItemId; }

private:
	/** 备注编辑提交 */
	void OnNoteTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	/** 名称编辑提交 */
	void OnNameTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	/** 创建 Tooltip widget */
	TSharedRef<SWidget> CreateToolTipWidget() const;

	/** 向 Tooltip InfoBox 添加信息 */
	void AddToToolTipInfoBox(const TSharedRef<SVerticalBox>& InfoBox, const FText& Key, const FText& Value, bool bImportant) const;

private:
	/** 节点数据 */
	TSharedPtr<FFavoritesTreeNode> Node;

	/** 备注可编辑文本 */
	TSharedPtr<SInlineEditableTextBlock> NoteEditableText;

	/** 名称可编辑文本 */
	TSharedPtr<SInlineEditableTextBlock> NameEditableText;

	/** 缩略图 */
	TSharedPtr<FAssetThumbnail> AssetThumbnail;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

	/** 
	 * 构造时缓存的 Item 数据 - 避免悬空指针问题
	 * 当 MergedCategories 数组被重建时，Node->Item 指针可能失效，
	 * 但 Slate 属性回调仍可能访问它。通过缓存关键数据，确保安全访问。
	 */
	FString CachedItemPath;
	FString CachedDisplayName;
	FString CachedNote;
	FGuid CachedItemId;
	FGuid CachedCategoryId;
	EFavoritesItemType CachedItemType = EFavoritesItemType::Asset;
	EFavoritesConfigSource CachedConfigSource = EFavoritesConfigSource::Local;
	bool bItemDataCached = false;

	/** 缓存的 AssetData */
	mutable FAssetData CachedAssetData;
	mutable bool bAssetDataCached = false;

	/** 缓存的资产图标 */
	mutable const FSlateBrush* CachedAssetIcon = nullptr;
	mutable bool bIconCached = false;

	/** 缓存的资产类型名 */
	mutable FText CachedAssetTypeName;
	mutable bool bTypeNameCached = false;

	/** 是否正在拖拽悬停 */
	bool bIsDragHovering = false;

	/** 拖拽插入位置：true=上方, false=下方 */
	bool bDragInsertAbove = false;

	/** 所属 TreeView 的弱引用，用于获取多选项目 */
	TWeakPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> OwnerTreeViewWeak;
};
