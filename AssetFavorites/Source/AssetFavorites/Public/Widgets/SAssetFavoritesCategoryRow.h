// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "AssetFavoritesTypes.h"

class SInlineEditableTextBlock;

/**
 * 分类行 Widget - 多列版本
 */
class SAssetFavoritesCategoryRow : public SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>
{
public:
	SLATE_BEGIN_ARGS(SAssetFavoritesCategoryRow)
		: _OwnerTreeView()
	{}
		SLATE_ARGUMENT(TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>>, OwnerTreeView)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FFavoritesTreeNode> InNode);

	// SMultiColumnTableRow 接口
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	// STableRow 接口 - 自定义背景
	virtual const FSlateBrush* GetBorder() const override;

	// SWidget 接口
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** 显示右键菜单 */
	void ShowContextMenu(const FGeometry& Geometry, const FPointerEvent& MouseEvent);

	/** 右键菜单操作 */
	void OnRenameCategory();
	void OnDeleteCategory();
	void OnSetCategoryColor(FLinearColor NewColor);

	/** 复制 / 粘贴 */
	void OnCopyCategories();
	void OnPasteCategories();
	void OnBatchAddPaths();

	/**
	 * 收集右键操作的目标分类：TreeView 里选中多个且当前行在选中集合内时取全部选中的分类，
	 * 否则只取当前行。结果只含分类 ID。
	 */
	TArray<FGuid> CollectTargetCategoryIds() const;

	/** 重命名提交 */
	void OnRenameTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	/** 删除按钮点击 */
	FReply OnDeleteClicked();

public:
	/** 进入编辑模式（供外部调用） */
	void EnterEditingMode();

	/** 获取缓存的分类ID */
	const FGuid& GetCachedCategoryId() const { return CachedCategoryId; }

private:
	/** 节点数据 */
	TSharedPtr<FFavoritesTreeNode> Node;

	/**
	 * 构造时缓存的 Category 数据 - 避免悬空指针问题
	 * 当 MergedCategories 数组被重建时，Node->Category 指针可能失效
	 */
	FGuid CachedCategoryId;
	FString CachedCategoryName;
	FLinearColor CachedCategoryColor = FLinearColor(0.2f, 0.6f, 1.0f);
	EFavoritesConfigSource CachedConfigSource = EFavoritesConfigSource::Local;
	int32 CachedItemCount = 0;
	bool bCategoryDataCached = false;

	/** 可编辑文本 */
	TSharedPtr<SInlineEditableTextBlock> EditableText;

	/** 是否正在拖拽悬停 */
	bool bIsDragHovering = false;

	/** 分组拖拽插入位置：true=上方, false=下方 */
	bool bCategoryDragInsertAbove = false;

	/** 是否有分组拖拽悬停（与 bIsDragHovering 区分：后者是资产拖入，前者是分组排序） */
	bool bIsCategoryDragHovering = false;

	/** 所属 TreeView 的弱引用，用于获取多选项目 */
	TWeakPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> OwnerTreeViewWeak;
};
