// Copyright LoF Team. All Rights Reserved.

#include "SAssetFavoritesPanel.h"
#include "SAssetFavoritesCategoryRow.h"
#include "SAssetFavoritesItemRow.h"
#include "AssetFavoritesDataManager.h"
#include "AssetFavoritesSettings.h"
#include "AssetFavoritesStyle.h"
#include "AssetFavoritesColumnIds.h"
#include "AssetFavoritesTransfer.h"
#include "AssetFavoritesPathImport.h"
#include "FavoriteCategoryDragDropOp.h"

#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/SHeaderRow.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ContentBrowserDataDragDropOp.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "AssetFavoritesPanel"

// 调试日志开关：设为 1 开启调试日志，设为 0 关闭
#define ASSET_FAVORITES_DEBUG_LOG 1

#if ASSET_FAVORITES_DEBUG_LOG
	#define AF_LOG(Format, ...) UE_LOG(LogTemp, Log, TEXT("AssetFavorites ") Format, ##__VA_ARGS__)
#else
	#define AF_LOG(Format, ...) 
#endif

void SAssetFavoritesPanel::Construct(const FArguments& InArgs)
{
	// 初始化进程信息缓存（PID + Log 路径，进程内不变）
	InitProcessInfoCache();

	// 创建缩略图池
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(128);

	// 加载数据
	FAssetFavoritesDataManager::Get().LoadData();

	// 绑定数据变更回调
	DataChangedHandle = FAssetFavoritesDataManager::Get().OnDataChanged.AddRaw(this, &SAssetFavoritesPanel::OnDataChanged);

	// 绑定设置变更回调
	SettingsChangedHandle = UAssetFavoritesSettings::Get()->OnSettingsChanged.AddRaw(this, &SAssetFavoritesPanel::OnSettingsChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		// 工具栏
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]
		// 分隔线
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2)
		[
			SNew(SSeparator)
		]
		// 树形视图
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildTreeView()
		]
	];

	// 初始化树形数据
	RefreshTreeData();
}

SAssetFavoritesPanel::~SAssetFavoritesPanel()
{
	if (DataChangedHandle.IsValid())
	{
		FAssetFavoritesDataManager::Get().OnDataChanged.Remove(DataChangedHandle);
	}
	if (SettingsChangedHandle.IsValid())
	{
		UAssetFavoritesSettings::Get()->OnSettingsChanged.Remove(SettingsChangedHandle);
	}
}

TSharedRef<SWidget> SAssetFavoritesPanel::BuildToolbar()
{
	return SNew(SVerticalBox)
		// 第一行：工具栏
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::ClipToBounds)

			// 显示备注开关（文字版）
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 2)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SAssetFavoritesPanel::GetShowNotesState)
				.OnCheckStateChanged(this, &SAssetFavoritesPanel::OnShowNotesToggled)
				.ToolTipText(LOCTEXT("ShowNotesTooltip", "显示/隐藏备注"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowNotesLabel", "备注"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			// 隐藏公共收藏组开关
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 2)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SAssetFavoritesPanel::GetHideSharedState)
				.OnCheckStateChanged(this, &SAssetFavoritesPanel::OnHideSharedToggled)
				.ToolTipText(LOCTEXT("HideSharedTooltip", "隐藏/显示公共收藏组"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HideSharedLabel", "隐藏公共"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			// 搜索框
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4, 2)
			.VAlign(VAlign_Center)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "搜索收藏..."))
				.OnTextChanged(this, &SAssetFavoritesPanel::OnSearchTextChanged)
				.OnTextCommitted(this, &SAssetFavoritesPanel::OnSearchTextCommitted)
			]

			// 全部展开按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 2)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesPanel::OnExpandAllClicked)
				.ToolTipText(LOCTEXT("ExpandAllTooltip", "展开所有分类"))
				.ContentPadding(FMargin(4, 2))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ExpandAllLabel", "展开"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			// 全部折叠按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 2)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesPanel::OnCollapseAllClicked)
				.ToolTipText(LOCTEXT("CollapseAllTooltip", "折叠所有分类"))
				.ContentPadding(FMargin(4, 2))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CollapseAllLabel", "折叠"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			// 批量添加路径
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 2)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesPanel::OnBatchAddPathsClicked)
				.ToolTipText(LOCTEXT("BatchAddPathsTooltip", "弹窗批量粘贴资产路径（P4 / 磁盘 / /Game）到当前分类"))
				.ContentPadding(FMargin(4, 2))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BatchAddPathsLabel", "批量添加"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			// 添加分类按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 2)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesPanel::OnAddCategoryClicked)
				.ToolTipText(LOCTEXT("AddCategoryTooltip", "添加新分类"))
				.ContentPadding(FMargin(2))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Plus"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]
		// 第二行：配置文件路径（可点击打开）+ 重新加载按钮
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 0, 4, 2)
		[
			SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::ClipToBounds)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHyperlink)
				.Text(this, &SAssetFavoritesPanel::GetConfigFilePathText)
				.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
				.ToolTipText(LOCTEXT("ConfigPathTooltip", "点击打开配置文件"))
				.OnNavigate(this, &SAssetFavoritesPanel::OnConfigFilePathClicked)
			]

			// 重新加载按钮（样式同展开/折叠按钮）
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesPanel::OnReloadClicked)
				.ToolTipText(LOCTEXT("ReloadTooltip", "重新加载：检查个人/共享配置是否被更新，并刷新全部收藏项的资产有效状态"))
				.ContentPadding(FMargin(4, 2))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ReloadLabel", "重新加载"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]
		]
		// 第三行：进程信息（PID + 复制按钮 + Log 路径超链接）
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 0, 4, 2)
		[
			SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::ClipToBounds)

			// PID 文本
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("PidLabelFormat", "PID: {0}"), FText::FromString(CachedPidString)))
				.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
				.ToolTipText(LOCTEXT("PidTooltip", "当前 UE 进程 ID"))
			]

			// 复制 PID 按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 8, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesPanel::OnCopyPidClicked)
				.ToolTipText(LOCTEXT("CopyPidTooltip", "复制 PID 到剪贴板"))
				.ContentPadding(FMargin(2))
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("GenericCommands.Copy"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			// Log 路径超链接
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHyperlink)
				.Text(FText::Format(LOCTEXT("LogPathFormat", "Log: {0}"),
					FText::FromString(CachedLogFilePath.IsEmpty() ? TEXT("<unavailable>") : CachedLogFilePath)))
				.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
				.ToolTipText(FText::FromString(CachedLogFilePath.IsEmpty()
					? FString(TEXT("Log 文件路径不可用"))
					: FString::Printf(TEXT("点击在资源管理器中定位:\n%s"), *CachedLogFilePath)))
				.OnNavigate(this, &SAssetFavoritesPanel::OnLogPathClicked)
			]
		];
}

TSharedRef<SWidget> SAssetFavoritesPanel::BuildTreeView()
{
	// 创建表头（列宽会在 Tick 中根据窗口宽度动态调整）
	HeaderRow = SNew(SHeaderRow)
		// 版本控制状态列
		+ SHeaderRow::Column(FavoritesColumnId::SourceControl)
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(20.0f)
		.HeaderContentPadding(FMargin(0))
		// 名称列（宽度由 UpdateColumnWidths 动态设置）
		+ SHeaderRow::Column(FavoritesColumnId::Name)
		.DefaultLabel(LOCTEXT("NameColumnHeader", "名称"))
		.ManualWidth(700.0f)
		.ShouldGenerateWidget(true)
		// 类型列（宽度由 UpdateColumnWidths 动态设置）
		+ SHeaderRow::Column(FavoritesColumnId::Type)
		.DefaultLabel(LOCTEXT("TypeColumnHeader", "类型"))
		.ManualWidth(200.0f)
		.ShouldGenerateWidget(true)
		// 定位按钮列
		+ SHeaderRow::Column(FavoritesColumnId::Locate)
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(24.0f)
		// 删除按钮列
		+ SHeaderRow::Column(FavoritesColumnId::Delete)
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(24.0f);

	// STreeView 自带滚动功能，表头固定不参与滚动
	SAssignNew(TreeView, STreeView<TSharedPtr<FFavoritesTreeNode>>)
		.TreeItemsSource(&FilteredRootNodes)
		.OnGenerateRow(this, &SAssetFavoritesPanel::OnGenerateRow)
		.OnGetChildren(this, &SAssetFavoritesPanel::OnGetChildren)
		.OnSelectionChanged(this, &SAssetFavoritesPanel::OnSelectionChanged)
		.OnExpansionChanged(this, &SAssetFavoritesPanel::OnExpansionChanged)
		.OnContextMenuOpening(this, &SAssetFavoritesPanel::OnContextMenuOpening)
		.SelectionMode(ESelectionMode::Multi)
		.HeaderRow(HeaderRow);

	return TreeView.ToSharedRef();
}

void SAssetFavoritesPanel::RefreshTreeData()
{
	RootNodes.Empty();

	bool bHideShared = UAssetFavoritesSettings::Get()->bHideSharedCategories;

	TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
	for (int32 CatIndex = 0; CatIndex < Categories.Num(); ++CatIndex)
	{
		FFavoritesCategory& Category = Categories[CatIndex];
		
		// 如果设置了隐藏公共收藏组，跳过公共分类
		if (bHideShared && Category.ConfigSource == EFavoritesConfigSource::Shared)
		{
			continue;
		}
		
		TSharedPtr<FFavoritesTreeNode> CategoryNode = FFavoritesTreeNode::CreateCategoryNode(&Category, CatIndex);

		// 添加子项
		for (int32 ItemIdx = 0; ItemIdx < Category.Items.Num(); ++ItemIdx)
		{
			FFavoritesItem& Item = Category.Items[ItemIdx];
			TSharedPtr<FFavoritesTreeNode> ItemNode = FFavoritesTreeNode::CreateItemNode(&Item, CatIndex, ItemIdx);
			ItemNode->Parent = CategoryNode;
			CategoryNode->Children.Add(ItemNode);
		}

		RootNodes.Add(CategoryNode);
	}

	FilterTreeData();
}

void SAssetFavoritesPanel::FilterTreeData()
{
	FilteredRootNodes.Empty();

	if (SearchText.IsEmpty())
	{
		FilteredRootNodes = RootNodes;
	}
	else
	{
		for (const TSharedPtr<FFavoritesTreeNode>& CategoryNode : RootNodes)
		{
			TSharedPtr<FFavoritesTreeNode> FilteredCategoryNode = MakeShareable(new FFavoritesTreeNode());
			FilteredCategoryNode->NodeType = FFavoritesTreeNode::ENodeType::Category;
			FilteredCategoryNode->Category = CategoryNode->Category;
			FilteredCategoryNode->CachedCategoryId = CategoryNode->CachedCategoryId;
			FilteredCategoryNode->CachedDisplayName = CategoryNode->CachedDisplayName;
			FilteredCategoryNode->CategoryIndex = CategoryNode->CategoryIndex;

			bool bCategoryMatches = CategoryNode->GetDisplayName().Contains(SearchText);

			for (const TSharedPtr<FFavoritesTreeNode>& ItemNode : CategoryNode->Children)
			{
				bool bItemMatches = ItemNode->GetDisplayName().Contains(SearchText);
				FString Note = ItemNode->GetNote();
				if (!Note.IsEmpty())
				{
					bItemMatches |= Note.Contains(SearchText);
				}

				if (bCategoryMatches || bItemMatches)
				{
					TSharedPtr<FFavoritesTreeNode> FilteredItemNode = MakeShareable(new FFavoritesTreeNode());
					FilteredItemNode->NodeType = FFavoritesTreeNode::ENodeType::Item;
					FilteredItemNode->Item = ItemNode->Item;
					FilteredItemNode->CachedItemPath = ItemNode->CachedItemPath;
					FilteredItemNode->CachedDisplayName = ItemNode->CachedDisplayName;
					FilteredItemNode->CachedNote = ItemNode->CachedNote;
					FilteredItemNode->CachedItemType = ItemNode->CachedItemType;
					FilteredItemNode->CategoryIndex = ItemNode->CategoryIndex;
					FilteredItemNode->ItemIndex = ItemNode->ItemIndex;
					FilteredItemNode->Parent = FilteredCategoryNode;
					FilteredCategoryNode->Children.Add(FilteredItemNode);
				}
			}

			if (bCategoryMatches || FilteredCategoryNode->Children.Num() > 0)
			{
				FilteredRootNodes.Add(FilteredCategoryNode);
			}
		}
	}

	if (TreeView.IsValid())
	{
		// 收集需要展开的节点
		// 注意：Node->Category 是裸指针，可能在数据刷新后失效（悬空）。
		// 这里通过 CachedCategoryId 从 DataManager 重新查找最新的 Category，避免崩溃。
		TArray<TSharedPtr<FFavoritesTreeNode>> NodesToExpand;
		for (const TSharedPtr<FFavoritesTreeNode>& Node : FilteredRootNodes)
		{
			if (!Node->IsCategory() || !Node->CachedCategoryId.IsValid())
			{
				continue;
			}

			FFavoritesCategory* LiveCategory = FAssetFavoritesDataManager::Get().FindCategoryById(Node->CachedCategoryId);
			if (!LiveCategory)
			{
				continue;
			}

			// 同步刷新节点上的裸指针，保持后续逻辑一致
			Node->Category = LiveCategory;

			AF_LOG("FilterTreeData: Category '%s' bExpanded=%d", *LiveCategory->Name, LiveCategory->bExpanded);
			if (LiveCategory->bExpanded)
			{
				NodesToExpand.Add(Node);
			}
		}
		
		// 设置标志，抑制 OnExpansionChanged 的保存操作
		// 因为 SetItemExpansion 会触发 OnExpansionChanged，而那里调用 MergeCategoriesData 会导致指针失效
		bRestoringExpansionState = true;
		
		// 清除所有展开状态（内部会调用 RequestTreeRefresh）
		TreeView->ClearExpandedItems();
		
		// 只展开需要展开的节点
		for (const TSharedPtr<FFavoritesTreeNode>& Node : NodesToExpand)
		{
			TreeView->SetItemExpansion(Node, true);
		}
		
		bRestoringExpansionState = false;
	}
}

TSharedRef<ITableRow> SAssetFavoritesPanel::OnGenerateRow(TSharedPtr<FFavoritesTreeNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// 验证数据有效性，避免悬空指针导致崩溃
	// TreeView 的异步刷新可能在数据已更新后才触发 OnGenerateRow
	// 注意：不能用裸指针 null 检查（悬空指针 ≠ nullptr），改用索引有效性判断
	if (Item->IsCategory())
	{
		if (Item->CategoryIndex == INDEX_NONE)
		{
			// 索引无效，返回空白行
			return SNew(STableRow<TSharedPtr<FFavoritesTreeNode>>, OwnerTable);
		}
		TSharedRef<SAssetFavoritesCategoryRow> CategoryRow = SNew(SAssetFavoritesCategoryRow, OwnerTable, Item)
			.OwnerTreeView(TreeView);
		
		// 检查是否是待编辑的新分类
		if (PendingEditCategoryId.IsValid() && CategoryRow->GetCachedCategoryId() == PendingEditCategoryId)
		{
			AF_LOG("OnGenerateRow: Found pending category, CachedCategoryId=%s", *CategoryRow->GetCachedCategoryId().ToString());
			// 缓存行引用，在 Tick 中延迟触发编辑模式
			PendingEditCategoryRow = CategoryRow;
		}
		
		return CategoryRow;
	}
	else
	{
		if (Item->CategoryIndex == INDEX_NONE || Item->ItemIndex == INDEX_NONE)
		{
			// 索引无效，返回空白行
			return SNew(STableRow<TSharedPtr<FFavoritesTreeNode>>, OwnerTable);
		}
		TSharedRef<SAssetFavoritesItemRow> ItemRow = SNew(SAssetFavoritesItemRow, OwnerTable, Item)
			.ThumbnailPool(ThumbnailPool)
			.OwnerTreeView(TreeView);
		
		// 检查是否是待编辑的 Item（添加备注后自动进入编辑模式）
		if (PendingEditItemId.IsValid() && ItemRow->GetCachedItemId() == PendingEditItemId)
		{
			AF_LOG("OnGenerateRow: Found pending item, CachedItemId=%s", *ItemRow->GetCachedItemId().ToString());
			// 缓存行引用，在 Tick 中延迟触发编辑模式
			PendingEditItemRow = ItemRow;
		}
		
		// 检查是否是待重命名的 Item（F2 键触发重命名）
		if (PendingRenameItemId.IsValid() && ItemRow->GetCachedItemId() == PendingRenameItemId)
		{
			AF_LOG("OnGenerateRow: Found pending rename item, CachedItemId=%s", *ItemRow->GetCachedItemId().ToString());
			// 缓存行引用，在 Tick 中延迟触发重命名编辑模式
			PendingRenameItemRow = ItemRow;
		}
		
		return ItemRow;
	}
}

void SAssetFavoritesPanel::OnGetChildren(TSharedPtr<FFavoritesTreeNode> Item, TArray<TSharedPtr<FFavoritesTreeNode>>& OutChildren)
{
	if (Item->IsCategory())
	{
		OutChildren = Item->Children;
	}
}

void SAssetFavoritesPanel::OnSelectionChanged(TSharedPtr<FFavoritesTreeNode> Item, ESelectInfo::Type SelectInfo)
{
	// 选择变更处理
}

TSharedPtr<SWidget> SAssetFavoritesPanel::OnContextMenuOpening()
{
	// 行上的右键菜单在鼠标按下时已经弹出；此时若已有菜单打开，就不再叠加面板菜单
	if (FSlateApplication::Get().AnyMenusVisible())
	{
		return nullptr;
	}

	// 菜单打开时只读一次剪贴板，避免每帧读取
	const bool bCanPaste = FAssetFavoritesTransfer::ClipboardHasFavorites();

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PanelPaste", "粘贴"),
		LOCTEXT("PanelPasteTooltip", "粘贴剪贴板中的分类：同名分类合并，其余追加到末尾"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([]() { FAssetFavoritesTransfer::PasteFromClipboard(FGuid()); }),
			FCanExecuteAction::CreateLambda([bCanPaste]() { return bCanPaste; })
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PanelBatchAddPaths", "批量添加路径..."),
		LOCTEXT("PanelBatchAddPathsTooltip", "弹窗粘贴多行资产路径，加入当前选中分类（无选中则加入第一个本地分类）"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { OnBatchAddPathsClicked(); }))
	);

	return MenuBuilder.MakeWidget();
}

void SAssetFavoritesPanel::OnExpansionChanged(TSharedPtr<FFavoritesTreeNode> Item, bool bExpanded)
{
	// 如果正在程序恢复展开状态，不需要保存（否则会触发 MergeCategoriesData 导致指针失效）
	if (bRestoringExpansionState)
	{
		AF_LOG("OnExpansionChanged: SKIPPED (restoring) CachedCategoryId='%s' bExpanded=%d", 
			*Item->CachedCategoryId.ToString(), bExpanded);
		return;
	}
	
	// 使用缓存的 CategoryId，而不是可能已失效的 Category 指针
	if (Item->IsCategory() && Item->CachedCategoryId.IsValid())
	{
		AF_LOG("OnExpansionChanged: SAVING CachedCategoryId='%s' bExpanded=%d", 
			*Item->CachedCategoryId.ToString(), bExpanded);
		FAssetFavoritesDataManager::Get().SetCategoryExpanded(Item->CachedCategoryId, bExpanded);
	}
}

void SAssetFavoritesPanel::OnShowNotesToggled(ECheckBoxState NewState)
{
	UAssetFavoritesSettings::Get()->bShowNotes = (NewState == ECheckBoxState::Checked);
	UAssetFavoritesSettings::Get()->SaveConfig();
	
	// 通知设置变更，触发完整的 TreeView 重建
	UAssetFavoritesSettings::Get()->OnSettingsChanged.Broadcast();
}

ECheckBoxState SAssetFavoritesPanel::GetShowNotesState() const
{
	return UAssetFavoritesSettings::Get()->bShowNotes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SAssetFavoritesPanel::OnHideSharedToggled(ECheckBoxState NewState)
{
	UAssetFavoritesSettings::Get()->bHideSharedCategories = (NewState == ECheckBoxState::Checked);
	UAssetFavoritesSettings::Get()->SaveConfig();
	
	// 触发刷新
	RefreshTreeData();
}

ECheckBoxState SAssetFavoritesPanel::GetHideSharedState() const
{
	return UAssetFavoritesSettings::Get()->bHideSharedCategories ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FReply SAssetFavoritesPanel::OnCollapseAllClicked()
{
	if (TreeView.IsValid())
	{
		// 折叠所有分类节点
		for (const TSharedPtr<FFavoritesTreeNode>& Node : FilteredRootNodes)
		{
			if (Node.IsValid() && Node->IsCategory())
			{
				TreeView->SetItemExpansion(Node, false);
			}
		}
	}
	return FReply::Handled();
}

FReply SAssetFavoritesPanel::OnExpandAllClicked()
{
	if (TreeView.IsValid())
	{
		// 展开所有分类节点
		for (const TSharedPtr<FFavoritesTreeNode>& Node : FilteredRootNodes)
		{
			if (Node.IsValid() && Node->IsCategory())
			{
				TreeView->SetItemExpansion(Node, true);
			}
		}
	}
	return FReply::Handled();
}

FReply SAssetFavoritesPanel::OnAddCategoryClicked()
{
	// 显示菜单让用户选择添加本地还是公共分类
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("AddCategory", LOCTEXT("AddCategorySection", "添加分类"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("AddLocalCategory", "添加本地分类"),
			LOCTEXT("AddLocalCategoryTooltip", "添加本地分类（仅保存在本地，不会提交到 Git）"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Lock"),
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				int32 NewCategoryIndex = FAssetFavoritesDataManager::Get().GetCategories().Num() + 1;
				FString NewCategoryName = FString::Printf(TEXT("Local Category %d"), NewCategoryIndex);
				FFavoritesCategory& NewCategory = FAssetFavoritesDataManager::Get().AddCategory(NewCategoryName, EFavoritesConfigSource::Local);
				// 记录待编辑的分类ID，用于在 OnGenerateRow 后自动进入编辑模式
				// 注意：AddCategory 内部会触发 OnDataChanged，此时 PendingEditCategoryId 还未设置
				// 所以需要在设置后手动请求 TreeView 刷新
				PendingEditCategoryId = NewCategory.UniqueId;
				AF_LOG("AddLocalCategory: Set PendingEditCategoryId=%s", *PendingEditCategoryId.ToString());
				if (TreeView.IsValid())
				{
					TreeView->RequestTreeRefresh();
				}
			}))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("AddSharedCategory", "添加公共分类"),
			LOCTEXT("AddSharedCategoryTooltip", "添加公共分类（会提交到 Git，团队成员可见）"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				int32 NewCategoryIndex = FAssetFavoritesDataManager::Get().GetCategories().Num() + 1;
				FString NewCategoryName = FString::Printf(TEXT("Shared Category %d"), NewCategoryIndex);
				FFavoritesCategory& NewCategory = FAssetFavoritesDataManager::Get().AddCategory(NewCategoryName, EFavoritesConfigSource::Shared);
				// 记录待编辑的分类ID，用于在 OnGenerateRow 后自动进入编辑模式
				// 注意：AddCategory 内部会触发 OnDataChanged，此时 PendingEditCategoryId 还未设置
				// 所以需要在设置后手动请求 TreeView 刷新
				PendingEditCategoryId = NewCategory.UniqueId;
				AF_LOG("AddSharedCategory: Set PendingEditCategoryId=%s", *PendingEditCategoryId.ToString());
				if (TreeView.IsValid())
				{
					TreeView->RequestTreeRefresh();
				}
			}))
		);
	}
	MenuBuilder.EndSection();

	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetChecked(AsShared(), WidgetPath);

	FSlateApplication::Get().PushMenu(
		AsShared(),
		WidgetPath,
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect::ContextMenu
	);

	return FReply::Handled();
}

FGuid SAssetFavoritesPanel::ResolveBatchAddTargetCategoryId() const
{
	if (TreeView.IsValid())
	{
		const TArray<TSharedPtr<FFavoritesTreeNode>> Selected = TreeView->GetSelectedItems();
		for (const TSharedPtr<FFavoritesTreeNode>& Node : Selected)
		{
			if (!Node.IsValid())
			{
				continue;
			}
			if (Node->IsCategory() && Node->CachedCategoryId.IsValid())
			{
				return Node->CachedCategoryId;
			}
			if (Node->IsItem())
			{
				if (const TSharedPtr<FFavoritesTreeNode> Parent = Node->Parent.Pin())
				{
					if (Parent->CachedCategoryId.IsValid())
					{
						return Parent->CachedCategoryId;
					}
				}
				const TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
				if (Categories.IsValidIndex(Node->CategoryIndex))
				{
					return Categories[Node->CategoryIndex].UniqueId;
				}
			}
		}
	}
	return FGuid();
}

FReply SAssetFavoritesPanel::OnBatchAddPathsClicked()
{
	FAssetFavoritesPathImport::ShowDialog(ResolveBatchAddTargetCategoryId());
	return FReply::Handled();
}

void SAssetFavoritesPanel::OnSearchTextChanged(const FText& NewText)
{
	SearchText = NewText.ToString();
	FilterTreeData();
}

void SAssetFavoritesPanel::OnSearchTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	SearchText = NewText.ToString();
	FilterTreeData();
}

FReply SAssetFavoritesPanel::OnReloadClicked()
{
	FAssetFavoritesTransfer::ReloadAll();
	return FReply::Handled();
}

void SAssetFavoritesPanel::OnDataChanged()
{
	// 检查是否有待编辑的 Item（从 DataManager 获取）
	FGuid PendingItemId = FAssetFavoritesDataManager::Get().ConsumePendingEditItemId();
	if (PendingItemId.IsValid())
	{
		PendingEditItemId = PendingItemId;
	}
	
	RefreshTreeData();
}

void SAssetFavoritesPanel::OnSettingsChanged()
{
	// 完整刷新树形数据，确保显示设置（如显示备注）生效
	RefreshTreeData();
}

FReply SAssetFavoritesPanel::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// 分组拖拽排序
	TSharedPtr<FFavoriteCategoryDragDropOp> CategoryDragDrop = DragDropEvent.GetOperationAs<FFavoriteCategoryDragDropOp>();
	if (CategoryDragDrop.IsValid())
	{
		return FReply::Handled();
	}

	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	TSharedPtr<FContentBrowserDataDragDropOp> ContentBrowserDragDrop = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>();

	if (AssetDragDrop.IsValid() || ContentBrowserDragDrop.IsValid())
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SAssetFavoritesPanel::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// 分组拖拽：拖到面板空白区域时，将分组移动到最末尾
	TSharedPtr<FFavoriteCategoryDragDropOp> CategoryDragDrop = DragDropEvent.GetOperationAs<FFavoriteCategoryDragDropOp>();
	if (CategoryDragDrop.IsValid())
	{
		// 找到该分组所属数据源中的分类总数，移动到最后一个位置
		TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
		EFavoritesConfigSource DraggedSource = EFavoritesConfigSource::Local;
		for (const FFavoritesCategory& Cat : Categories)
		{
			if (Cat.UniqueId == CategoryDragDrop->DraggedCategoryId)
			{
				DraggedSource = Cat.ConfigSource;
				break;
			}
		}
		// 计算同源分类的数量
		int32 SameSourceCount = 0;
		for (const FFavoritesCategory& Cat : Categories)
		{
			if (Cat.ConfigSource == DraggedSource)
			{
				SameSourceCount++;
			}
		}
		// 移动到最后位置
		FAssetFavoritesDataManager::Get().MoveCategoryPosition(CategoryDragDrop->DraggedCategoryId, SameSourceCount - 1);
		return FReply::Handled();
	}

	// 优先检查 FContentBrowserDataDragDropOp (它继承自 FAssetDragDropOp，支持目录和文件)
	TSharedPtr<FContentBrowserDataDragDropOp> ContentBrowserDragDrop = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>();
	if (ContentBrowserDragDrop.IsValid())
	{
		// 如果没有分类，先创建一个默认分类
		TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
		if (Categories.Num() == 0)
		{
			FAssetFavoritesDataManager::Get().AddCategory(TEXT("Default"));
		}

		FFavoritesCategory* TargetCategory = &FAssetFavoritesDataManager::Get().GetCategories()[0];

		// 处理拖拽的内容
		const TArray<FContentBrowserItem>& DraggedItems = ContentBrowserDragDrop->GetDraggedItems();
		TArray<FAssetData> Assets;

		for (const FContentBrowserItem& Item : DraggedItems)
		{
			if (Item.IsFolder())
			{
				// 目录
				FName VirtualPath = Item.GetVirtualPath();
				AddDirectoryToCategory(VirtualPath.ToString(), TargetCategory);
			}
			else if (Item.IsFile())
			{
				// 文件
				FAssetData AssetData;
				if (Item.Legacy_TryGetAssetData(AssetData))
				{
					Assets.Add(AssetData);
				}
			}
		}

		if (Assets.Num() > 0)
		{
			AddAssetsToCategory(Assets, TargetCategory);
		}

		return FReply::Handled();
	}

	// 再检查普通的 FAssetDragDropOp (用于从其他地方拖入的资产)
	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (AssetDragDrop.IsValid() && AssetDragDrop->GetAssets().Num() > 0)
	{
		// 如果没有分类，先创建一个默认分类
		TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
		if (Categories.Num() == 0)
		{
			FAssetFavoritesDataManager::Get().AddCategory(TEXT("Default"));
		}

		// 添加到第一个分类
		FFavoritesCategory* TargetCategory = &FAssetFavoritesDataManager::Get().GetCategories()[0];
		AddAssetsToCategory(AssetDragDrop->GetAssets(), TargetCategory);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAssetFavoritesPanel::AddAssetsToCategory(const TArray<FAssetData>& Assets, FFavoritesCategory* TargetCategory)
{
	if (!TargetCategory)
	{
		return;
	}

	for (const FAssetData& Asset : Assets)
	{
		FString AssetPath = Asset.GetSoftObjectPath().ToString();
		FAssetFavoritesDataManager::Get().AddItem(TargetCategory->UniqueId, AssetPath, EFavoritesItemType::Asset);
	}
}

void SAssetFavoritesPanel::AddDirectoryToCategory(const FString& DirectoryPath, FFavoritesCategory* TargetCategory)
{
	if (!TargetCategory)
	{
		return;
	}

	FAssetFavoritesDataManager::Get().AddItem(TargetCategory->UniqueId, DirectoryPath, EFavoritesItemType::Directory);
}

FReply SAssetFavoritesPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Ctrl+S: 保存选中的标脏资产
	if (InKeyEvent.GetKey() == EKeys::S && InKeyEvent.IsControlDown())
	{
		SaveSelectedDirtyAssets();
		return FReply::Handled();
	}

	// Ctrl+B: 在 Content Browser 中定位选中项
	if (InKeyEvent.GetKey() == EKeys::B && InKeyEvent.IsControlDown())
	{
		BrowseToSelectedItems();
		return FReply::Handled();
	}

	// Delete: 删除选中项
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		if (TreeView.IsValid())
		{
			FAssetFavoritesDataManager::Get().RemoveSelectedItems(TreeView->GetSelectedItems());
		}
		return FReply::Handled();
	}

	// F2: 重命名选中项或分类
	if (InKeyEvent.GetKey() == EKeys::F2)
	{
		if (TreeView.IsValid())
		{
			TArray<TSharedPtr<FFavoritesTreeNode>> SelectedNodes = TreeView->GetSelectedItems();
			if (SelectedNodes.Num() == 1)
			{
				TSharedPtr<FFavoritesTreeNode> Node = SelectedNodes[0];
				if (Node->IsCategory() && Node->CategoryIndex != INDEX_NONE)
				{
					// 直接通过 WidgetFromItem 获取已有的行 Widget（无需 RequestTreeRefresh）
					TSharedPtr<ITableRow> TableRow = TreeView->WidgetFromItem(Node);
					if (TableRow.IsValid())
					{
						TSharedRef<SWidget> RowWidget = TableRow->AsWidget();
						TSharedRef<SAssetFavoritesCategoryRow> CategoryRow = StaticCastSharedRef<SAssetFavoritesCategoryRow>(RowWidget);
						CategoryRow->EnterEditingMode();
						return FReply::Handled();
					}
					// Widget 还不可见（如被滚动出视口），走 Pending 延迟路径
					TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
					if (Categories.IsValidIndex(Node->CategoryIndex))
					{
						PendingEditCategoryId = Categories[Node->CategoryIndex].UniqueId;
						PendingEditCategoryRow.Reset();
						TreeView->RequestTreeRefresh();
					}
					return FReply::Handled();
				}
				else if (Node->IsItem() && Node->CategoryIndex != INDEX_NONE && Node->ItemIndex != INDEX_NONE)
				{
					// 直接通过 WidgetFromItem 获取已有的行 Widget
					TSharedPtr<ITableRow> TableRow = TreeView->WidgetFromItem(Node);
					if (TableRow.IsValid())
					{
						TSharedRef<SWidget> RowWidget = TableRow->AsWidget();
						TSharedRef<SAssetFavoritesItemRow> ItemRow = StaticCastSharedRef<SAssetFavoritesItemRow>(RowWidget);
						ItemRow->EnterNameEditingMode();
						return FReply::Handled();
					}
					// Widget 还不可见，走 Pending 延迟路径
					TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
					if (Categories.IsValidIndex(Node->CategoryIndex))
					{
						FFavoritesCategory& Category = Categories[Node->CategoryIndex];
						if (Category.Items.IsValidIndex(Node->ItemIndex))
						{
							PendingRenameItemId = Category.Items[Node->ItemIndex].UniqueId;
							PendingRenameItemRow.Reset();
							TreeView->RequestTreeRefresh();
						}
					}
					return FReply::Handled();
				}
			}
		}
		return FReply::Handled(); // 即使未处理也消耗按键，避免传递
	}

	return FReply::Unhandled();
}

void SAssetFavoritesPanel::SaveSelectedDirtyAssets()
{
	if (!TreeView.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FFavoritesTreeNode>> SelectedItems = TreeView->GetSelectedItems();
	TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();

	// 收集选中项中标脏的资产包
	TArray<UPackage*> PackagesToSave;

	for (const TSharedPtr<FFavoritesTreeNode>& Node : SelectedItems)
	{
		if (!Node->IsItem() || Node->CategoryIndex == INDEX_NONE || Node->ItemIndex == INDEX_NONE)
		{
			continue;
		}

		if (!Categories.IsValidIndex(Node->CategoryIndex))
		{
			continue;
		}
		FFavoritesCategory& Category = Categories[Node->CategoryIndex];
		if (!Category.Items.IsValidIndex(Node->ItemIndex))
		{
			continue;
		}
		FFavoritesItem& Item = Category.Items[Node->ItemIndex];

		if (Item.ItemType != EFavoritesItemType::Asset)
		{
			continue;
		}

		// 从资产路径提取包名
		FString PackageName = Item.Path;
		int32 DotIndex;
		if (PackageName.FindLastChar('.', DotIndex))
		{
			PackageName = PackageName.Left(DotIndex);
		}

		// 查找已加载的包
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package && Package->IsDirty())
		{
			PackagesToSave.AddUnique(Package);
		}
	}

	if (PackagesToSave.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}
}

void SAssetFavoritesPanel::BrowseToSelectedItems()
{
	if (!TreeView.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FFavoritesTreeNode>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
	
	TArray<FAssetData> AssetsToSync;
	TArray<FString> FoldersToSync;

	for (const TSharedPtr<FFavoritesTreeNode>& Node : SelectedItems)
	{
		if (!Node->IsItem() || Node->CategoryIndex == INDEX_NONE || Node->ItemIndex == INDEX_NONE)
		{
			continue;
		}

		// 通过索引安全获取数据
		if (!Categories.IsValidIndex(Node->CategoryIndex))
		{
			continue;
		}
		FFavoritesCategory& Category = Categories[Node->CategoryIndex];
		if (!Category.Items.IsValidIndex(Node->ItemIndex))
		{
			continue;
		}
		FFavoritesItem& Item = Category.Items[Node->ItemIndex];

		if (Item.ItemType == EFavoritesItemType::Directory)
		{
			// SyncBrowserToFolders 需要包路径格式（如 /Game/xxx），不是虚拟路径（/All/Game/xxx）
			FString FolderPath = Item.Path;
			if (FolderPath.StartsWith(TEXT("/All/")))
			{
				FolderPath = FolderPath.Mid(4); // 去掉 "/All" 前缀
			}
			FoldersToSync.Add(FolderPath);
		}
		else
		{
			FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(Item.Path);
			if (AssetData.IsValid())
			{
				AssetsToSync.Add(AssetData);
			}
		}
	}

	// 优先定位到资产
	if (AssetsToSync.Num() > 0)
	{
		ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);
	}
	else if (FoldersToSync.Num() > 0)
	{
		ContentBrowserModule.Get().SyncBrowserToFolders(FoldersToSync, true);
	}
}

FText SAssetFavoritesPanel::GetConfigFilePathText() const
{
	FString LocalPath = FAssetFavoritesDataManager::Get().GetLocalDataFilePath();
	FString SharedPath = FAssetFavoritesDataManager::Get().GetSharedDataFilePath();
	return FText::Format(LOCTEXT("ConfigPathFormat", "本地: {0} | 公共: {1}"), 
		FText::FromString(FPaths::GetCleanFilename(LocalPath)),
		FText::FromString(FPaths::GetCleanFilename(SharedPath)));
}

void SAssetFavoritesPanel::OnConfigFilePathClicked()
{
	// 打开本地配置文件所在目录
	FString ConfigPath = FAssetFavoritesDataManager::Get().GetLocalDataFilePath();
	
	// 转换为绝对路径
	ConfigPath = FPaths::ConvertRelativePathToFull(ConfigPath);
	
	// 确保文件存在
	if (!FPaths::FileExists(ConfigPath))
	{
		// 如果文件不存在，先保存一次创建它
		FAssetFavoritesDataManager::Get().SaveData();
	}
	
	// 在资源管理器中选中该文件
	FPlatformProcess::ExploreFolder(*ConfigPath);
}

void SAssetFavoritesPanel::InitProcessInfoCache()
{
	// PID
	const uint32 ProcessId = FPlatformProcess::GetCurrentProcessId();
	CachedPidString = FString::Printf(TEXT("%u"), ProcessId);

	// Log 文件绝对路径
	const FString RawLogPath = FPlatformOutputDevices::GetAbsoluteLogFilename();
	CachedLogFilePath = RawLogPath.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(RawLogPath);
}

FReply SAssetFavoritesPanel::OnCopyPidClicked()
{
	// 复制 PID 到剪贴板
	FPlatformApplicationMisc::ClipboardCopy(*CachedPidString);

	// 弹出轻量提示
	FNotificationInfo Info(FText::Format(LOCTEXT("PidCopiedNotify", "已复制 PID: {0}"), FText::FromString(CachedPidString)));
	Info.ExpireDuration = 1.5f;
	Info.bUseSuccessFailIcons = true;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_Success);
	}

	return FReply::Handled();
}

void SAssetFavoritesPanel::OnLogPathClicked()
{
	// Log 路径不可用时，不执行任何操作
	if (CachedLogFilePath.IsEmpty())
	{
		return;
	}

	// 在资源管理器中选中 log 文件（与配置文件路径行为一致）
	FPlatformProcess::ExploreFolder(*CachedLogFilePath);
}

void SAssetFavoritesPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// 延迟触发新分类的编辑模式
	// 注意：TreeView 刷新是异步的，OnGenerateRow 可能在多个 Tick 后才被调用
	// 所以只有当行引用有效时才清除待编辑状态
	if (PendingEditCategoryId.IsValid())
	{
		AF_LOG("Tick: PendingEditCategoryId=%s, PendingEditCategoryRow.IsValid=%d", 
			*PendingEditCategoryId.ToString(), PendingEditCategoryRow.IsValid());
		if (TSharedPtr<SAssetFavoritesCategoryRow> CategoryRow = PendingEditCategoryRow.Pin())
		{
			AF_LOG("Tick: Triggering CategoryRow->EnterEditingMode()");
			CategoryRow->EnterEditingMode();
			// 成功触发后清除状态
			PendingEditCategoryId.Invalidate();
			PendingEditCategoryRow.Reset();
		}
		// 如果行还未生成，继续等待（不清除状态）
	}

	// 延迟触发 Item 备注的编辑模式
	if (PendingEditItemId.IsValid())
	{
		AF_LOG("Tick: PendingEditItemId=%s, PendingEditItemRow.IsValid=%d", 
			*PendingEditItemId.ToString(), PendingEditItemRow.IsValid());
		if (TSharedPtr<SAssetFavoritesItemRow> ItemRow = PendingEditItemRow.Pin())
		{
			AF_LOG("Tick: Triggering ItemRow->EnterNoteEditingMode()");
			ItemRow->EnterNoteEditingMode();
			// 成功触发后清除状态
			PendingEditItemId.Invalidate();
			PendingEditItemRow.Reset();
		}
		// 如果行还未生成，继续等待（不清除状态）
	}

	// 延迟触发 Item 名称的编辑模式（F2 键重命名）
	if (PendingRenameItemId.IsValid())
	{
		AF_LOG("Tick: PendingRenameItemId=%s, PendingRenameItemRow.IsValid=%d", 
			*PendingRenameItemId.ToString(), PendingRenameItemRow.IsValid());
		if (TSharedPtr<SAssetFavoritesItemRow> ItemRow = PendingRenameItemRow.Pin())
		{
			AF_LOG("Tick: Triggering ItemRow->EnterNameEditingMode()");
			ItemRow->EnterNameEditingMode();
			// 成功触发后清除状态
			PendingRenameItemId.Invalidate();
			PendingRenameItemRow.Reset();
		}
		// 如果行还未生成，继续等待（不清除状态）
	}

	const float CurrentWidth = AllottedGeometry.GetLocalSize().X;
	
	// 只在宽度变化超过阈值时更新（避免频繁更新）
	constexpr float WidthChangeThreshold = 1.0f;
	if (FMath::Abs(CurrentWidth - LastPanelWidth) > WidthChangeThreshold)
	{
		LastPanelWidth = CurrentWidth;
		UpdateColumnWidths(CurrentWidth);
	}
}

void SAssetFavoritesPanel::UpdateColumnWidths(float AvailableWidth)
{
	if (!HeaderRow.IsValid())
	{
		return;
	}

	// 计算可用于可调整列的总宽度（减去固定宽度列和滚动条预留）
	constexpr float ScrollBarWidth = 16.0f;
	const float AdjustableWidth = FMath::Max(0.0f, AvailableWidth - FixedColumnsWidth - ScrollBarWidth);

	// 按比例计算列宽
	float NameWidth = AdjustableWidth * NameColumnRatio;
	float TypeWidth = AdjustableWidth * (1.0f - NameColumnRatio);

	// 应用最小/最大阈值
	NameWidth = FMath::Clamp(NameWidth, NameColumnMinWidth, NameColumnMaxWidth);
	TypeWidth = FMath::Clamp(TypeWidth, TypeColumnMinWidth, TypeColumnMaxWidth);

	// 更新列宽
	HeaderRow->SetColumnWidth(FavoritesColumnId::Name, NameWidth);
	HeaderRow->SetColumnWidth(FavoritesColumnId::Type, TypeWidth);
}

#undef LOCTEXT_NAMESPACE
