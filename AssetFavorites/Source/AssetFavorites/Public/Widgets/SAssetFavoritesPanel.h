// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "AssetFavoritesTypes.h"

class SSearchBox;
class SHeaderRow;
class FAssetThumbnailPool;

/**
 * 收藏夹主面板
 */
class SAssetFavoritesPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetFavoritesPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAssetFavoritesPanel();

	// SWidget 接口
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** 构建工具栏 */
	TSharedRef<SWidget> BuildToolbar();

	/** 构建树形视图 */
	TSharedRef<SWidget> BuildTreeView();

	/** 刷新树形数据 */
	void RefreshTreeData();

	/** 过滤树形数据 */
	void FilterTreeData();

	/** TreeView 回调 */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FFavoritesTreeNode> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FFavoritesTreeNode> Item, TArray<TSharedPtr<FFavoritesTreeNode>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FFavoritesTreeNode> Item, ESelectInfo::Type SelectInfo);
	void OnExpansionChanged(TSharedPtr<FFavoritesTreeNode> Item, bool bExpanded);

	/** 面板空白处右键菜单：粘贴 / 重新加载 */
	TSharedPtr<SWidget> OnContextMenuOpening();

	/** 工具栏回调 */
	void OnShowNotesToggled(ECheckBoxState NewState);
	ECheckBoxState GetShowNotesState() const;
	void OnHideSharedToggled(ECheckBoxState NewState);
	ECheckBoxState GetHideSharedState() const;
	FReply OnCollapseAllClicked();
	FReply OnExpandAllClicked();
	FReply OnAddCategoryClicked();
	FReply OnBatchAddPathsClicked();

	/** 工具栏/空白处批量添加：优先当前选中分类，否则第一个本地分类 */
	FGuid ResolveBatchAddTargetCategoryId() const;

	/** 搜索回调 */
	void OnSearchTextChanged(const FText& NewText);
	void OnSearchTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	/** 数据变更回调 */
	void OnDataChanged();

	/** 设置变更回调 */
	void OnSettingsChanged();

	/** 添加资产到默认分类 */
	void AddAssetsToCategory(const TArray<FAssetData>& Assets, FFavoritesCategory* TargetCategory);

	/** 添加目录到默认分类 */
	void AddDirectoryToCategory(const FString& DirectoryPath, FFavoritesCategory* TargetCategory);

	/** 保存选中的标脏资产 */
	void SaveSelectedDirtyAssets();

	/** 定位选中项到 Content Browser */
	void BrowseToSelectedItems();

	/** 获取配置文件路径文本 */
	FText GetConfigFilePathText() const;

	/** 配置文件路径点击回调 */
	void OnConfigFilePathClicked();

	/** 重新加载按钮（配置路径行末尾） */
	FReply OnReloadClicked();

	/** 初始化进程信息缓存（PID 与 Log 路径，进程内不变） */
	void InitProcessInfoCache();

	/** 复制 PID 到剪贴板 */
	FReply OnCopyPidClicked();

	/** Log 路径点击回调（在资源管理器中定位） */
	void OnLogPathClicked();

	/** 更新列宽以适应窗口宽度 */
	void UpdateColumnWidths(float AvailableWidth);

private:
	/** TreeView 控件 */
	TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> TreeView;

	/** 表头行 */
	TSharedPtr<SHeaderRow> HeaderRow;

	/** 搜索框 */
	TSharedPtr<SSearchBox> SearchBox;

	/** 缩略图池 */
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

	/** 树形数据源（原始） */
	TArray<TSharedPtr<FFavoritesTreeNode>> RootNodes;

	/** 树形数据源（过滤后） */
	TArray<TSharedPtr<FFavoritesTreeNode>> FilteredRootNodes;

	/** 当前搜索文本 */
	FString SearchText;

	/** 数据变更委托句柄 */
	FDelegateHandle DataChangedHandle;

	/** 设置变更委托句柄 */
	FDelegateHandle SettingsChangedHandle;

	/** 是否正在恢复展开状态（抑制 OnExpansionChanged 的保存操作） */
	bool bRestoringExpansionState = false;

	/** 上一次的窗口宽度（用于检测宽度变化） */
	float LastPanelWidth = 0.0f;

	/** 名称列占可调整宽度的比例（0.0 - 1.0） */
	static constexpr float NameColumnRatio = 0.78f;

	/** 固定宽度列的总宽度（SourceControl + Locate + Delete） */
	static constexpr float FixedColumnsWidth = 20.0f + 24.0f + 24.0f;

	/** 名称列的最小/最大宽度 */
	static constexpr float NameColumnMinWidth = 150.0f;
	static constexpr float NameColumnMaxWidth = 1200.0f;

	/** 类型列的最小/最大宽度 */
	static constexpr float TypeColumnMinWidth = 80.0f;
	static constexpr float TypeColumnMaxWidth = 400.0f;

	/** 待编辑的新分类ID（添加分类后自动进入编辑模式） */
	FGuid PendingEditCategoryId;

	/** 最近生成的分类行缓存（用于延迟触发编辑模式） */
	TWeakPtr<class SAssetFavoritesCategoryRow> PendingEditCategoryRow;

	/** 待编辑的 Item ID（添加备注后自动进入编辑模式） */
	FGuid PendingEditItemId;

	/** 最近生成的 Item 行缓存（用于延迟触发编辑模式） */
	TWeakPtr<class SAssetFavoritesItemRow> PendingEditItemRow;

	/** 待重命名的 Item ID（F2 键触发重命名） */
	FGuid PendingRenameItemId;

	/** 最近生成的 Item 行缓存（用于延迟触发重命名编辑模式） */
	TWeakPtr<class SAssetFavoritesItemRow> PendingRenameItemRow;

	/** 缓存：当前进程 PID 字符串 */
	FString CachedPidString;

	/** 缓存：当前 UE 进程 Log 文件绝对路径 */
	FString CachedLogFilePath;

	/** 缓存：进程信息行的 FText（PID + Log 路径，组合后一次性构造） */
	FText CachedProcessInfoText;
};
