// Copyright LoF Team. All Rights Reserved.

#include "SAssetFavoritesItemRow.h"
#include "AssetFavoritesDataManager.h"
#include "AssetFavoritesSettings.h"
#include "AssetFavoritesStyle.h"
#include "AssetFavoritesColumnIds.h"
#include "FavoriteItemDragDropOp.h"

#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Experimental/ContentBrowserExtensionUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTypeActions.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ContentBrowserDataDragDropOp.h"
#include "AssetFavoritesSourceControlUtils.h"
#include "AssetThumbnail.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "AssetFavoritesItemRow"

void SAssetFavoritesItemRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FFavoritesTreeNode> InNode)
{
	Node = InNode;
	ThumbnailPool = InArgs._ThumbnailPool;
	OwnerTreeViewWeak = InArgs._OwnerTreeView;

	// 构造时通过索引从 DataManager 安全获取 Item 数据
	// 不直接使用 Node->Item 指针，因为它可能已经是悬空指针
	if (Node.IsValid() && Node->CategoryIndex != INDEX_NONE && Node->ItemIndex != INDEX_NONE)
	{
		TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
		if (Categories.IsValidIndex(Node->CategoryIndex))
		{
			FFavoritesCategory& Category = Categories[Node->CategoryIndex];
			if (Category.Items.IsValidIndex(Node->ItemIndex))
			{
				FFavoritesItem& Item = Category.Items[Node->ItemIndex];
				CachedItemPath = Item.Path;
				CachedDisplayName = Item.DisplayName;
				CachedNote = Item.Note;
				CachedItemId = Item.UniqueId;
				CachedCategoryId = Category.UniqueId;
				CachedItemType = Item.ItemType;
				CachedConfigSource = Category.ConfigSource;
				bItemDataCached = true;
			}
		}
	}

	// 对于资产类型，创建缩略图
	if (bItemDataCached && CachedItemType == EFavoritesItemType::Asset)
	{
		FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(CachedItemPath);
		if (AssetData.IsValid())
		{
			CachedItemPath = AssetData.GetSoftObjectPath().ToString();
			CachedAssetData = AssetData;
			bAssetDataCached = true;
			
			// 使用共享的 ThumbnailPool 或全局的
			TSharedPtr<FAssetThumbnailPool> Pool = ThumbnailPool.IsValid() ? ThumbnailPool : UThumbnailManager::Get().GetSharedThumbnailPool();
			AssetThumbnail = MakeShared<FAssetThumbnail>(AssetData, 32, 32, Pool);
		}
	}

	SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::Construct(
		FSuperRowType::FArguments()
			.Padding(FMargin(0, 1))
			.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row")),
		InOwnerTable
	);

	// 设置自定义 Tooltip
	SetToolTip(
		SNew(SToolTip)
		.TextMargin(1)
		.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.ToolTipBorder"))
		[
			CreateToolTipWidget()
		]
	);
}

TSharedRef<SWidget> SAssetFavoritesItemRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	// 使用构造时缓存的数据，避免悬空指针问题
	if (!Node.IsValid() || !bItemDataCached)
	{
		return SNullWidget::NullWidget;
	}

	float UIScale = UAssetFavoritesSettings::Get()->UIScale;
	int32 FontSize = UAssetFavoritesSettings::Get()->FontSize;
	bool bShowNotes = UAssetFavoritesSettings::Get()->bShowNotes;
	bool bShowIcons = UAssetFavoritesSettings::Get()->bShowAssetIcons;

	// 版本控制状态列
	if (ColumnName == FavoritesColumnId::SourceControl)
	{
		return SNew(SBox)
			.WidthOverride(20.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(this, &SAssetFavoritesItemRow::GetSourceControlIcon)
				.DesiredSizeOverride(FVector2D(12, 12))
			];
	}

	// 名称列
	if (ColumnName == FavoritesColumnId::Name)
	{
		TSharedRef<SHorizontalBox> NameContent = SNew(SHorizontalBox);

		// 图标/缩略图
		if (bShowIcons)
		{
			const float IconSize = 32 * UIScale;
			
			// 对于资产，显示缩略图；对于文件夹，显示图标
			// 使用缓存的 ItemType 以确保一致性
			if (CachedItemType == EFavoritesItemType::Asset && AssetThumbnail.IsValid())
			{
				FAssetThumbnailConfig ThumbnailConfig;
				ThumbnailConfig.bAllowFadeIn = false;
				const float DirtyOverlaySize = IconSize * 0.5f;
				
				NameContent->AddSlot()
					.AutoWidth()
					.Padding(4 * UIScale, 0, 4, 0)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(IconSize)
						.HeightOverride(IconSize)
						[
							SNew(SOverlay)

							+ SOverlay::Slot()
							[
								AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig)
							]

							// 标脏图标 (左下角)
							+ SOverlay::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Bottom)
							[
								SNew(SBox)
								.WidthOverride(DirtyOverlaySize)
								.HeightOverride(DirtyOverlaySize)
								.Visibility(this, &SAssetFavoritesItemRow::GetDirtyBadgeVisibility)
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush(TEXT("ContentBrowser.ContentDirty")))
								]
							]
						]
					];
			}
			else
			{
				// 文件夹使用图标
				NameContent->AddSlot()
					.AutoWidth()
					.Padding(4 * UIScale, 0, 4, 0)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(this, &SAssetFavoritesItemRow::GetAssetIcon)
						.ColorAndOpacity(this, &SAssetFavoritesItemRow::GetFolderIconColor)
						.DesiredSizeOverride(FVector2D(IconSize, IconSize))
					];
			}
		}

		// 名称和备注
		TSharedRef<SVerticalBox> TextContent = SNew(SVerticalBox);

		// 显示名称（可编辑，带标脏前缀 *）
		TextContent->AddSlot()
			.AutoHeight()
			[
				SAssignNew(NameEditableText, SInlineEditableTextBlock)
				.Text(this, &SAssetFavoritesItemRow::GetDisplayNameText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", FontSize))
				.OnTextCommitted(this, &SAssetFavoritesItemRow::OnNameTextCommitted)
			];

		// 备注（如果启用且有内容）
		if (bShowNotes && !CachedNote.IsEmpty())
		{
			TextContent->AddSlot()
				.AutoHeight()
				[
					SAssignNew(NoteEditableText, SInlineEditableTextBlock)
					.Text(FText::FromString(CachedNote))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", FontSize - 2))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.OnTextCommitted(this, &SAssetFavoritesItemRow::OnNoteTextCommitted)
				];
		}

		NameContent->AddSlot()
			.FillWidth(1.0f)
			.Padding(4, 0)
			.VAlign(VAlign_Center)
			[
				TextContent
			];

		return NameContent;
	}

	// 类型列
	if (ColumnName == FavoritesColumnId::Type)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(4, 0))
			[
				SNew(STextBlock)
				.Text(this, &SAssetFavoritesItemRow::GetAssetTypeName)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", FontSize - 1))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	// 定位按钮列
	if (ColumnName == FavoritesColumnId::Locate)
	{
		return SNew(SBox)
			.WidthOverride(24.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesItemRow::OnLocateClicked)
				.ToolTipText(LOCTEXT("LocateTooltip", "在 Content Browser 中定位"))
				.ContentPadding(FMargin(2))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Search"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}

	// 删除按钮列
	if (ColumnName == FavoritesColumnId::Delete)
	{
		return SNew(SBox)
			.WidthOverride(24.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SAssetFavoritesItemRow::OnDeleteClicked)
				.ToolTipText(LOCTEXT("DeleteTooltip", "从收藏中移除"))
				.ContentPadding(FMargin(2))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.X"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}

	return SNullWidget::NullWidget;
}

const FSlateBrush* SAssetFavoritesItemRow::GetAssetIcon() const
{
	if (bIconCached)
	{
		return CachedAssetIcon;
	}

	bIconCached = true;

	// 使用构造时缓存的数据
	if (!bItemDataCached)
	{
		CachedAssetIcon = FAppStyle::GetBrush("Icons.Help");
		return CachedAssetIcon;
	}

	if (CachedItemType == EFavoritesItemType::Directory)
	{
		// 使用 Content Browser 的文件夹图标（收起状态）
		CachedAssetIcon = FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
		return CachedAssetIcon;
	}

	// 获取资产图标
	FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(CachedItemPath);

	if (AssetData.IsValid())
	{
		CachedAssetIcon = FAppStyle::GetBrush(AssetData.AssetClassPath.GetAssetName());
		if (!CachedAssetIcon || CachedAssetIcon == FAppStyle::GetDefaultBrush())
		{
			// 尝试从 ClassThumbnailBrush 获取
			static const FName AssetIconName = FName(TEXT("ClassIcon.Object"));
			CachedAssetIcon = FAppStyle::GetBrush(AssetIconName);
		}
	}
	else
	{
		CachedAssetIcon = FAppStyle::GetBrush("Icons.Warning");
	}

	return CachedAssetIcon;
}

FSlateColor SAssetFavoritesItemRow::GetFolderIconColor() const
{
	// 使用构造时缓存的数据
	if (!bItemDataCached)
	{
		return FSlateColor::UseForeground();
	}

	// 只有文件夹才需要检查自定义颜色
	if (CachedItemType == EFavoritesItemType::Directory)
	{
		// 将虚拟路径（如 /All/Game/xxx）转换为内部路径（如 /Game/xxx）
		// GetFolderColor API 需要内部路径格式
		FString InternalPath = CachedItemPath;
		if (InternalPath.StartsWith(TEXT("/All/")))
		{
			InternalPath = InternalPath.Mid(4); // 去掉 "/All" 前缀
		}
		
		// 从路径获取自定义颜色（与 Content Browser 保持一致）
		TOptional<FLinearColor> Color = UE::Editor::ContentBrowser::ExtensionUtils::GetFolderColor(FName(*InternalPath));
		if (Color.IsSet())
		{
			return Color.GetValue();
		}
	}

	// 默认颜色（白色，不改变图标原色）
	return FSlateColor::UseForeground();
}

const FSlateBrush* SAssetFavoritesItemRow::GetSourceControlIcon() const
{
	if (!bItemDataCached || CachedItemType != EFavoritesItemType::Asset)
	{
		return nullptr;
	}

	// 无效收藏不要查 SCC。Provider.GetState 内部可能对失效包名调用 LongPackageNameToFilename，映射失败即 Fatal。
	if (!bAssetDataCached || !CachedAssetData.IsValid())
	{
		return nullptr;
	}

	const FString PackageName = CachedAssetData.PackageName.ToString();
	if (PackageName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots*/false))
	{
		return nullptr;
	}

	FString FilePath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, FilePath, FPackageName::GetAssetPackageExtension()))
	{
		return nullptr;
	}

	FilePath = FPaths::ConvertRelativePathToFull(FilePath);
	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		return nullptr;
	}

	return FAssetFavoritesSourceControlUtils::GetSourceControlIconForFile(FilePath);
}

FText SAssetFavoritesItemRow::GetAssetTypeName() const
{
	if (bTypeNameCached)
	{
		return CachedAssetTypeName;
	}

	bTypeNameCached = true;

	// 使用构造时缓存的数据
	if (!bItemDataCached)
	{
		CachedAssetTypeName = FText::GetEmpty();
		return CachedAssetTypeName;
	}

	if (CachedItemType == EFavoritesItemType::Directory)
	{
		CachedAssetTypeName = LOCTEXT("DirectoryType", "文件夹");
		return CachedAssetTypeName;
	}

	// 获取资产数据
	FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(CachedItemPath);

	if (AssetData.IsValid())
	{
		// 尝试获取 AssetTypeActions 中的显示名称
		const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetData.GetClass()).Pin();
		
		if (AssetTypeActions.IsValid())
		{
			CachedAssetTypeName = AssetTypeActions->GetName();
		}
		else
		{
			// 使用类名
			CachedAssetTypeName = FText::FromName(AssetData.AssetClassPath.GetAssetName());
		}
	}
	else
	{
		CachedAssetTypeName = LOCTEXT("InvalidAsset", "无效");
	}

	return CachedAssetTypeName;
}

FText SAssetFavoritesItemRow::GetDisplayNameText() const
{
	if (!bItemDataCached)
	{
		return FText::GetEmpty();
	}

	return FText::FromString(CachedDisplayName);
}

bool SAssetFavoritesItemRow::IsPackageDirty() const
{
	if (!bItemDataCached || CachedItemType != EFavoritesItemType::Asset)
	{
		return false;
	}

	// 从资产路径提取包名
	FString PackageName = CachedItemPath;
	int32 DotIndex;
	if (PackageName.FindLastChar('.', DotIndex))
	{
		PackageName = PackageName.Left(DotIndex);
	}

	// 查找已加载的包（不触发加载）
	UPackage* Package = FindPackage(nullptr, *PackageName);
	if (Package)
	{
		return Package->IsDirty();
	}

	return false;
}

EVisibility SAssetFavoritesItemRow::GetDirtyBadgeVisibility() const
{
	return IsPackageDirty() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SAssetFavoritesItemRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		ShowContextMenu(MyGeometry, MouseEvent);
		return FReply::Handled();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// 先调用基类处理选中逻辑
		FReply Reply = SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::OnMouseButtonDown(MyGeometry, MouseEvent);
		// 在选中的基础上支持拖拽检测
		return Reply.DetectDrag(AsShared(), EKeys::LeftMouseButton);
	}

	return SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SAssetFavoritesItemRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Node.IsValid() || !Node->IsItem())
	{
		return FReply::Unhandled();
	}

	// 获取选中的所有项
	TArray<TSharedPtr<FFavoritesTreeNode>> DraggedNodes;
	
	// 如果有 TreeView 引用，获取所有选中的项
	TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> TreeView = OwnerTreeViewWeak.Pin();
	if (TreeView.IsValid())
	{
		TArray<TSharedPtr<FFavoritesTreeNode>> SelectedItems = TreeView->GetSelectedItems();
		// 只收集 Item 类型的节点
		for (const TSharedPtr<FFavoritesTreeNode>& SelectedNode : SelectedItems)
		{
			if (SelectedNode.IsValid() && SelectedNode->IsItem())
			{
				DraggedNodes.Add(SelectedNode);
			}
		}
	}
	
	// 如果没有获取到选中项（可能 TreeView 引用无效），至少添加当前节点
	if (DraggedNodes.Num() == 0)
	{
		DraggedNodes.Add(Node);
	}
	
	// 按界面显示顺序排序（先按 CategoryIndex，再按 ItemIndex）
	// 这样拖拽到目标分组后能保持原来的上下顺序
	DraggedNodes.Sort([](const TSharedPtr<FFavoritesTreeNode>& A, const TSharedPtr<FFavoritesTreeNode>& B)
	{
		if (A->CategoryIndex != B->CategoryIndex)
		{
			return A->CategoryIndex < B->CategoryIndex;
		}
		return A->ItemIndex < B->ItemIndex;
	});

	// 创建拖拽操作
	TSharedRef<FFavoriteItemDragDropOp> DragDropOp = FFavoriteItemDragDropOp::New(DraggedNodes);

	return FReply::Handled().BeginDragDrop(DragDropOp);
}

FReply SAssetFavoritesItemRow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FFavoriteItemDragDropOp> FavoriteItemDragDrop = DragDropEvent.GetOperationAs<FFavoriteItemDragDropOp>();
	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	TSharedPtr<FContentBrowserDataDragDropOp> ContentBrowserDragDrop = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>();

	if (FavoriteItemDragDrop.IsValid() || AssetDragDrop.IsValid() || ContentBrowserDragDrop.IsValid())
	{
		bIsDragHovering = true;

		// 计算鼠标位于行的上半部分还是下半部分
		FVector2D LocalPos = MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());
		float HalfHeight = MyGeometry.GetLocalSize().Y / 2.0f;
		bDragInsertAbove = LocalPos.Y < HalfHeight;

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAssetFavoritesItemRow::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	bIsDragHovering = false;
}

FReply SAssetFavoritesItemRow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bIsDragHovering = false;

	// 使用缓存数据进行验证
	if (!Node.IsValid() || !Node->IsItem() || !bItemDataCached || Node->CategoryIndex == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
	if (!Categories.IsValidIndex(Node->CategoryIndex))
	{
		return FReply::Unhandled();
	}

	FFavoritesCategory& TargetCategory = Categories[Node->CategoryIndex];

	// 计算目标位置 - 使用缓存的 ItemId
	int32 TargetItemIndex = TargetCategory.Items.IndexOfByPredicate([this](const FFavoritesItem& Item)
	{
		return Item.UniqueId == CachedItemId;
	});

	if (TargetItemIndex == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	// 如果是插入到下方，索引+1
	int32 InsertPosition = bDragInsertAbove ? TargetItemIndex : TargetItemIndex + 1;

	// 处理内部拖拽操作（收藏项移动/排序）
	TSharedPtr<FFavoriteItemDragDropOp> FavoriteItemDragDrop = DragDropEvent.GetOperationAs<FFavoriteItemDragDropOp>();
	if (FavoriteItemDragDrop.IsValid())
	{
		// 先收集所有要移动的项目信息
		// 因为每次 MoveItem 都会触发 MergeCategoriesData，导致索引失效
		struct FMoveInfo
		{
			FGuid SourceCategoryId;
			FGuid ItemId;
			bool bSameCategory;
		};
		TArray<FMoveInfo> ItemsToMove;

		for (const TSharedPtr<FFavoritesTreeNode>& DraggedNode : FavoriteItemDragDrop->DraggedNodes)
		{
			if (DraggedNode->IsItem() && DraggedNode->CategoryIndex != INDEX_NONE && DraggedNode->ItemIndex != INDEX_NONE)
			{
				// 通过索引安全获取拖拽项的数据
				if (!Categories.IsValidIndex(DraggedNode->CategoryIndex))
				{
					continue;
				}
				FFavoritesCategory& DraggedCategory = Categories[DraggedNode->CategoryIndex];
				if (!DraggedCategory.Items.IsValidIndex(DraggedNode->ItemIndex))
				{
					continue;
				}
				FFavoritesItem& DraggedItem = DraggedCategory.Items[DraggedNode->ItemIndex];

				// 不能拖到自己
				if (DraggedItem.UniqueId == CachedItemId)
				{
					continue;
				}

				FMoveInfo Info;
				Info.SourceCategoryId = DraggedCategory.UniqueId;
				Info.ItemId = DraggedItem.UniqueId;
				Info.bSameCategory = (DraggedNode->CategoryIndex == Node->CategoryIndex);
				ItemsToMove.Add(Info);
			}
		}

		// 批量执行移动，每次插入位置递增以保持相对顺序
		int32 CurrentInsertPosition = InsertPosition;
		for (const FMoveInfo& Info : ItemsToMove)
		{
			if (Info.bSameCategory)
			{
				// 同一分组内移动，调整顺序
				FAssetFavoritesDataManager::Get().MoveItemPosition(
					CachedCategoryId,
					Info.ItemId,
					CurrentInsertPosition
				);
			}
			else
			{
				// 不同分组间移动，使用原子操作移动到目标分类的指定位置
				FAssetFavoritesDataManager::Get().MoveItemToPosition(
					Info.SourceCategoryId,
					Info.ItemId,
					CachedCategoryId,
					CurrentInsertPosition
				);
			}
			CurrentInsertPosition++;
		}
		return FReply::Handled();
	}

	// 处理从 Content Browser 拖入
	TSharedPtr<FContentBrowserDataDragDropOp> ContentBrowserDragDrop = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>();
	if (ContentBrowserDragDrop.IsValid())
	{
		const TArray<FContentBrowserItem>& DraggedItems = ContentBrowserDragDrop->GetDraggedItems();
		
		for (const FContentBrowserItem& Item : DraggedItems)
		{
			FFavoritesItem* NewItem = nullptr;
			if (Item.IsFolder())
			{
				FName VirtualPath = Item.GetVirtualPath();
				NewItem = FAssetFavoritesDataManager::Get().AddItem(
					TargetCategory.UniqueId, 
					VirtualPath.ToString(), 
					EFavoritesItemType::Directory
				);
			}
			else if (Item.IsFile())
			{
				FAssetData AssetData;
				if (Item.Legacy_TryGetAssetData(AssetData))
				{
					FString AssetPath = AssetData.GetSoftObjectPath().ToString();
					NewItem = FAssetFavoritesDataManager::Get().AddItem(
						TargetCategory.UniqueId, 
						AssetPath, 
						EFavoritesItemType::Asset
					);
				}
			}

			// 调整新添加项的位置
			if (NewItem)
			{
				FAssetFavoritesDataManager::Get().MoveItemPosition(
					TargetCategory.UniqueId,
					NewItem->UniqueId,
					InsertPosition
				);
				InsertPosition++; // 下一个项放在后面
			}
		}
		return FReply::Handled();
	}

	// 处理普通资产拖入
	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (AssetDragDrop.IsValid())
	{
		for (const FAssetData& Asset : AssetDragDrop->GetAssets())
		{
			FString AssetPath = Asset.GetSoftObjectPath().ToString();
			FFavoritesItem* NewItem = FAssetFavoritesDataManager::Get().AddItem(
				TargetCategory.UniqueId, 
				AssetPath, 
				EFavoritesItemType::Asset
			);

			// 调整新添加项的位置
			if (NewItem)
			{
				FAssetFavoritesDataManager::Get().MoveItemPosition(
					TargetCategory.UniqueId,
					NewItem->UniqueId,
					InsertPosition
				);
				InsertPosition++;
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SAssetFavoritesItemRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && UAssetFavoritesSettings::Get()->bDoubleClickToOpen)
	{
		if (bItemDataCached)
		{
			if (CachedItemType == EFavoritesItemType::Directory)
			{
				// 目录双击时在 Content Browser 中定位
				OnLocateInContentBrowser();
			}
			else
			{
				// 资产双击时打开
				OnOpenAsset();
			}
		}
		return FReply::Handled();
	}

	return SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

void SAssetFavoritesItemRow::ShowContextMenu(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	if (!bItemDataCached)
	{
		return;
	}

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("ItemActions", LOCTEXT("ItemActionsSection", "收藏项操作"));
	{
		if (CachedItemType == EFavoritesItemType::Asset)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenAsset", "打开"),
				LOCTEXT("OpenAssetTooltip", "在编辑器中打开资产"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnOpenAsset))
			);
		}
		else if (CachedItemType == EFavoritesItemType::Directory)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenDirectory", "打开目录"),
				LOCTEXT("OpenDirectoryTooltip", "在 Content Browser 中打开此目录"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnLocateInContentBrowser))
			);
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("LocateInContentBrowser", "在 Content Browser 中定位"),
			LOCTEXT("LocateInContentBrowserTooltip", "在 Content Browser 中显示此项"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnLocateInContentBrowser))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("EditNote", "编辑备注"),
			LOCTEXT("EditNoteTooltip", "编辑此项的备注"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnEditNote))
		);

		// 只有当备注不为空时才显示"删除备注"选项
		if (!CachedNote.IsEmpty())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("RemoveNote", "删除备注"),
				LOCTEXT("RemoveNoteTooltip", "删除此项的备注"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnRemoveNote))
			);
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteItem", "从收藏中移除"),
			LOCTEXT("DeleteItemTooltip", "从收藏夹中移除此项"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnDeleteItem))
		);
	}
	MenuBuilder.EndSection();

	// 复制路径分区（对齐 UE Content Browser 的 Copy Reference / Object Path / Package Path / File Path）
	// 多选场景下，只要选中集合中至少有一个 Asset，就显示 Copy Reference / Copy Object Path
	MenuBuilder.BeginSection("CopyPaths", LOCTEXT("CopyPathsSection", "复制路径"));
	{
		const TArray<TSharedPtr<FFavoritesTreeNode>> CopyTargets = CollectCopyTargetNodes();
		const bool bHasAsset = CopyTargets.ContainsByPredicate([](const TSharedPtr<FFavoritesTreeNode>& InNode)
		{
			return InNode.IsValid() && InNode->CachedItemType == EFavoritesItemType::Asset;
		});

		// Copy Reference / Copy Object Path 仅对资产可用（目录无对象引用/对象路径）
		if (bHasAsset)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CopyReference", "Copy Reference"),
				LOCTEXT("CopyReferenceTooltip", "复制资产引用，格式：Class'/Game/Path/Asset.Asset'"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
				FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnCopyReference))
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CopyObjectPath", "Copy Object Path"),
				LOCTEXT("CopyObjectPathTooltip", "复制资产对象路径，格式：/Game/Path/Asset.Asset"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
				FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnCopyObjectPath))
			);
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyPackagePath", "Copy Package Path"),
			LOCTEXT("CopyPackagePathTooltip", "复制包路径，格式：/Game/Path/Asset"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnCopyPackagePath))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyFilePath", "Copy File Path"),
			LOCTEXT("CopyFilePathTooltip", "复制磁盘上的完整文件路径"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesItemRow::OnCopyFilePath))
		);
	}
	MenuBuilder.EndSection();

	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetChecked(AsShared(), WidgetPath);

	FSlateApplication::Get().PushMenu(
		AsShared(),
		WidgetPath,
		MenuBuilder.MakeWidget(),
		MouseEvent.GetScreenSpacePosition(),
		FPopupTransitionEffect::ContextMenu
	);
}

void SAssetFavoritesItemRow::OnOpenAsset()
{
	if (!bItemDataCached || CachedItemType != EFavoritesItemType::Asset)
	{
		return;
	}

	FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(CachedItemPath);

	if (AssetData.IsValid())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(AssetData.GetAsset());
	}
}

void SAssetFavoritesItemRow::OnLocateInContentBrowser()
{
	if (!bItemDataCached)
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	if (CachedItemType == EFavoritesItemType::Directory)
	{
		// SyncBrowserToFolders 需要包路径格式（如 /Game/xxx），不是虚拟路径（/All/Game/xxx）
		// 将虚拟路径转换为包路径
		FString FolderPath = CachedItemPath;
		if (FolderPath.StartsWith(TEXT("/All/")))
		{
			FolderPath = FolderPath.Mid(4); // 去掉 "/All" 前缀，保留 "/Game/xxx" 部分
		}
		ContentBrowserModule.Get().SyncBrowserToFolders({ FolderPath }, true);
	}
	else
	{
		FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(CachedItemPath);

		if (AssetData.IsValid())
		{
			TArray<FAssetData> Assets = { AssetData };
			ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
		}
	}
}

void SAssetFavoritesItemRow::OnEditNote()
{
	EnterNoteEditingMode();
}

void SAssetFavoritesItemRow::OnRemoveNote()
{
	if (!bItemDataCached)
	{
		return;
	}

	// 清空备注
	FAssetFavoritesDataManager::Get().SetItemNote(CachedCategoryId, CachedItemId, TEXT(""));
}

void SAssetFavoritesItemRow::EnterNoteEditingMode()
{
	if (NoteEditableText.IsValid())
	{
		NoteEditableText->EnterEditingMode();
	}
	else
	{
		// 如果备注文本控件不存在（备注为空），先添加一个默认备注触发刷新
		// 设置待编辑 Item ID，Panel 会在刷新后自动让新行进入编辑模式
		if (bItemDataCached)
		{
			// 设置待编辑状态
			FAssetFavoritesDataManager::Get().SetPendingEditItemId(CachedItemId);
			// 添加备注触发刷新
			FAssetFavoritesDataManager::Get().SetItemNote(
				CachedCategoryId,
				CachedItemId,
				TEXT("备注")
			);
		}
	}
}

void SAssetFavoritesItemRow::EnterNameEditingMode()
{
	if (NameEditableText.IsValid())
	{
		NameEditableText->EnterEditingMode();
	}
}

void SAssetFavoritesItemRow::OnDeleteItem()
{
	if (!bItemDataCached)
	{
		return;
	}

	// 获取所有选中的项并批量删除
	TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> TreeView = OwnerTreeViewWeak.Pin();
	if (TreeView.IsValid())
	{
		TArray<TSharedPtr<FFavoritesTreeNode>> SelectedItems = TreeView->GetSelectedItems();
		
		// 如果有多个选中项，删除所有选中项
		if (SelectedItems.Num() > 1)
		{
			FAssetFavoritesDataManager::Get().RemoveSelectedItems(SelectedItems);
			return;
		}
	}
	
	// 单选或无法获取选中项时，只删除当前项
	FAssetFavoritesDataManager::Get().RemoveItem(CachedCategoryId, CachedItemId);
}

namespace
{
	/** 从 FFavoritesItem 的 Path 字段提取 PackageName（去掉 .ObjectName 后缀）。
	 *  Asset 类型 Path 形如 "/Game/.../Foo.Foo"，Directory 类型 Path 形如 "/Game/..."。 */
	static FString ExtractPackageName(const FString& InItemPath)
	{
		int32 DotIndex = INDEX_NONE;
		if (InItemPath.FindChar(TEXT('.'), DotIndex))
		{
			return InItemPath.Left(DotIndex);
		}
		return InItemPath;
	}
}

TArray<TSharedPtr<FFavoritesTreeNode>> SAssetFavoritesItemRow::CollectCopyTargetNodes() const
{
	TArray<TSharedPtr<FFavoritesTreeNode>> Result;

	// 优先使用 TreeView 的多选结果
	TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> TreeView = OwnerTreeViewWeak.Pin();
	if (TreeView.IsValid())
	{
		TArray<TSharedPtr<FFavoritesTreeNode>> Selected = TreeView->GetSelectedItems();
		// 仅当多选且当前行也在选中集合中时使用多选；否则回退到当前行（符合 UE 右键单击未选中行时的常规预期）
		const bool bCurrentInSelection = Selected.ContainsByPredicate([this](const TSharedPtr<FFavoritesTreeNode>& InNode)
		{
			return InNode.IsValid() && InNode == Node;
		});

		if (Selected.Num() > 1 && bCurrentInSelection)
		{
			for (const TSharedPtr<FFavoritesTreeNode>& SelNode : Selected)
			{
				if (SelNode.IsValid() && SelNode->IsItem())
				{
					Result.Add(SelNode);
				}
			}
			return Result;
		}
	}

	// 单选/无 TreeView：仅当前行
	if (Node.IsValid() && Node->IsItem())
	{
		Result.Add(Node);
	}
	return Result;
}

FString SAssetFavoritesItemRow::FormatPathForNode(const TSharedPtr<FFavoritesTreeNode>& InNode, ECopyPathFormat Format)
{
	if (!InNode.IsValid() || InNode->CachedItemPath.IsEmpty())
	{
		return FString();
	}

	const FString& ItemPath = InNode->CachedItemPath;
	const EFavoritesItemType ItemType = InNode->CachedItemType;

	FString ResolvedPath = ItemPath;
	if (ItemType == EFavoritesItemType::Asset)
	{
		const FAssetData ResolvedAsset = FAssetFavoritesDataManager::ResolveAssetData(ItemPath);
		if (ResolvedAsset.IsValid())
		{
			ResolvedPath = ResolvedAsset.GetSoftObjectPath().ToString();
		}
	}

	switch (Format)
	{
	case ECopyPathFormat::Reference:
	{
		// 仅对 Asset 有意义
		if (ItemType != EFavoritesItemType::Asset)
		{
			return FString();
		}
		const FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(ItemPath);
		if (AssetData.IsValid())
		{
			return AssetData.GetExportTextName();
		}
		// 回退：无法解析到资产时退化为纯对象路径
		return ItemPath;
	}
	case ECopyPathFormat::ObjectPath:
	{
		// 仅对 Asset 有意义
		if (ItemType != EFavoritesItemType::Asset)
		{
			return FString();
		}
		return ResolvedPath;
	}
	case ECopyPathFormat::PackagePath:
	{
		return ExtractPackageName(ResolvedPath);
	}
	case ECopyPathFormat::FilePath:
	{
		const FString PackageName = ExtractPackageName(ResolvedPath);
		if (PackageName.IsEmpty())
		{
			return FString();
		}
		FString FileOnDisk;
		if (ItemType == EFavoritesItemType::Directory)
		{
			if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName / TEXT(""), FileOnDisk))
			{
				return FString();
			}
		}
		else
		{
			if (!FPackageName::TryConvertLongPackageNameToFilename(
				PackageName, FileOnDisk, FPackageName::GetAssetPackageExtension()))
			{
				return FString();
			}
		}
		// 与 UE 原生 Content Browser 的 Copy File Path 行为一致：转为绝对路径再做平台分隔符标准化
		// 参考：ContentBrowserUtils::CopyFilePathsToClipboard (EngineLoF/.../ContentBrowserUtils.cpp:554)
		FileOnDisk = FPaths::ConvertRelativePathToFull(FileOnDisk);
		FPaths::MakePlatformFilename(FileOnDisk);
		return FileOnDisk;
	}
	}
	return FString();
}

void SAssetFavoritesItemRow::CopySelectedPaths(ECopyPathFormat Format) const
{
	const TArray<TSharedPtr<FFavoritesTreeNode>> Targets = CollectCopyTargetNodes();
	if (Targets.Num() == 0)
	{
		return;
	}

	TArray<FString> Lines;
	Lines.Reserve(Targets.Num());
	for (const TSharedPtr<FFavoritesTreeNode>& Target : Targets)
	{
		const FString Line = FormatPathForNode(Target, Format);
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
	}

	if (Lines.Num() == 0)
	{
		return;
	}

	// 多个路径用换行符（LINE_TERMINATOR）连接，便于贴到代码/Excel/编辑器
	const FString ClipboardText = FString::Join(Lines, LINE_TERMINATOR);
	FPlatformApplicationMisc::ClipboardCopy(*ClipboardText);
}

void SAssetFavoritesItemRow::OnCopyReference()
{
	CopySelectedPaths(ECopyPathFormat::Reference);
}

void SAssetFavoritesItemRow::OnCopyObjectPath()
{
	CopySelectedPaths(ECopyPathFormat::ObjectPath);
}

void SAssetFavoritesItemRow::OnCopyPackagePath()
{
	CopySelectedPaths(ECopyPathFormat::PackagePath);
}

void SAssetFavoritesItemRow::OnCopyFilePath()
{
	CopySelectedPaths(ECopyPathFormat::FilePath);
}

FReply SAssetFavoritesItemRow::OnLocateClicked()
{
	OnLocateInContentBrowser();
	return FReply::Handled();
}

FReply SAssetFavoritesItemRow::OnDeleteClicked()
{
	OnDeleteItem();
	return FReply::Handled();
}

void SAssetFavoritesItemRow::OnNoteTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
	{
		if (bItemDataCached)
		{
			FAssetFavoritesDataManager::Get().SetItemNote(
				CachedCategoryId,
				CachedItemId,
				NewText.ToString()
			);
		}
	}
}

void SAssetFavoritesItemRow::OnNameTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
	{
		if (bItemDataCached)
		{
			// 去掉标脏前缀 *（编辑模式初始文本可能带有 * 前缀）
			FString NewName = NewText.ToString();
			if (NewName.StartsWith(TEXT("*")))
			{
				NewName.RemoveFromStart(TEXT("*"));
			}
			
			FAssetFavoritesDataManager::Get().SetItemDisplayName(
				CachedCategoryId,
				CachedItemId,
				NewName
			);
		}
	}
}

int32 SAssetFavoritesItemRow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 先绘制基类内容
	LayerId = SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// 绘制拖拽插入指示线
	if (bIsDragHovering)
	{
		const float LineThickness = 2.0f;
		const FLinearColor LineColor(0.2f, 0.6f, 1.0f, 1.0f); // 蓝色

		FVector2D LineStart, LineEnd;
		if (bDragInsertAbove)
		{
			// 在顶部绘制线
			LineStart = FVector2D(0, 0);
			LineEnd = FVector2D(AllottedGeometry.GetLocalSize().X, 0);
		}
		else
		{
			// 在底部绘制线
			LineStart = FVector2D(0, AllottedGeometry.GetLocalSize().Y);
			LineEnd = FVector2D(AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y);
		}

		TArray<FVector2D> LinePoints;
		LinePoints.Add(LineStart);
		LinePoints.Add(LineEnd);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			LineColor,
			true,
			LineThickness
		);
	}

	return LayerId;
}

TSharedRef<SWidget> SAssetFavoritesItemRow::CreateToolTipWidget() const
{
	// 使用缓存数据确保安全
	if (!bItemDataCached)
	{
		return SNew(STextBlock).Text(LOCTEXT("InvalidItem", "无效项"));
	}

	// 对于文件夹，显示简单提示
	if (CachedItemType == EFavoritesItemType::Directory)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(CachedDisplayName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 0)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("FolderPath", "路径: {0}"), FText::FromString(CachedItemPath)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	// 对于资产，显示详细信息
	FAssetData ItemAssetData;
	if (bAssetDataCached)
	{
		ItemAssetData = CachedAssetData;
	}
	else
	{
		ItemAssetData = FAssetFavoritesDataManager::ResolveAssetData(CachedItemPath);
	}

	if (!ItemAssetData.IsValid())
	{
		return SNew(STextBlock).Text(LOCTEXT("InvalidAsset", "资产未找到"));
	}

	// 获取类型名
	FText ClassText = FText::GetEmpty();
	if (FAssetToolsModule::IsModuleLoaded())
	{
		TWeakPtr<IAssetTypeActions> AssetTypeActions = FAssetToolsModule::GetModule().Get().GetAssetTypeActionsForClass(ItemAssetData.GetClass());
		if (AssetTypeActions.IsValid())
		{
			ClassText = AssetTypeActions.Pin()->GetName();
		}
	}
	if (ClassText.IsEmpty())
	{
		ClassText = FText::FromName(ItemAssetData.AssetClassPath.GetAssetName());
	}

	// 创建 InfoBox
	TSharedRef<SVerticalBox> InfoBox = SNew(SVerticalBox);
	
	// 路径
	AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipPath", "路径"), FText::FromName(ItemAssetData.PackagePath), false);

	// 资产路径长度
	const FString PackagePath = ItemAssetData.PackageName.ToString();
	int32 PathLength = PackagePath.Len();
	int32 MaxPathLen = 260; // 通用最大路径长度
	AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipPathLength", "资产文件路径长度"), 
		FText::Format(LOCTEXT("PathLengthValue", "{0} / {1}"), FText::AsNumber(PathLength), FText::AsNumber(MaxPathLen)), 
		PathLength > MaxPathLen);

	// 磁盘空间（如果可获取）
	FString FilePath;
	if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FilePath, FPackageName::GetAssetPackageExtension())
		&& FPaths::FileExists(FilePath))
	{
		int64 FileSize = IFileManager::Get().FileSize(*FilePath);
		if (FileSize > 0)
		{
			FText SizeText;
			if (FileSize < 1024)
			{
				SizeText = FText::Format(LOCTEXT("FileSizeBytes", "{0} B"), FText::AsNumber(FileSize));
			}
			else if (FileSize < 1024 * 1024)
			{
				FNumberFormattingOptions FormatOptions = FNumberFormattingOptions::DefaultNoGrouping();
				FormatOptions.SetMaximumFractionalDigits(2);
				SizeText = FText::Format(LOCTEXT("FileSizeKB", "{0} KiB"), FText::AsNumber(FileSize / 1024.0, &FormatOptions));
			}
			else
			{
				FNumberFormattingOptions FormatOptions = FNumberFormattingOptions::DefaultNoGrouping();
				FormatOptions.SetMaximumFractionalDigits(2);
				SizeText = FText::Format(LOCTEXT("FileSizeMB", "{0} MiB"), FText::AsNumber(FileSize / (1024.0 * 1024.0), &FormatOptions));
			}
			AddToToolTipInfoBox(InfoBox, LOCTEXT("TooltipDiskSpace", "磁盘空间"), SizeText, false);
		}
	}

	// 获取资产标签信息
	for (const auto& TagPair : ItemAssetData.TagsAndValues)
	{
		// 只显示一些重要的标签
		static const TSet<FName> ImportantTags = {
			FName("Role"),
			FName("NativeParentClass"),
			FName("ParentClass"),
		};
		
		if (ImportantTags.Contains(TagPair.Key))
		{
			AddToToolTipInfoBox(InfoBox, FText::FromName(TagPair.Key), FText::FromString(TagPair.Value.GetValue()), false);
		}
	}

	// 构建完整的 Tooltip
	TSharedRef<SVerticalBox> TooltipContent = SNew(SVerticalBox);

	// 判断是否改过名（DisplayName 与资产真实名不同）
	const FString RealAssetName = ItemAssetData.AssetName.ToString();
	const bool bHasCustomName = (CachedDisplayName != RealAssetName);

	// 名称信息区
	TSharedRef<SVerticalBox> NameBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(CachedDisplayName))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		];

	// 如果改过名，显示真实资产名
	if (bHasCustomName)
	{
		NameBox->AddSlot()
			.AutoHeight()
			.Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("TooltipRealName", "原名: {0}"), FText::FromString(RealAssetName)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	// 类型行
	NameBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("ContentBrowser.AssetActions.Edit"))
				.DesiredSizeOverride(FVector2D(12, 12))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(ClassText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];

	// 标题区域（名称 + 类型）
	TooltipContent->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SBorder)
			.Padding(6)
			.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
			[
				SNew(SHorizontalBox)
				// 缩略图
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SBox)
					.WidthOverride(64)
					.HeightOverride(64)
					[
						AssetThumbnail.IsValid() 
							? AssetThumbnail->MakeThumbnailWidget(FAssetThumbnailConfig())
							: SNullWidget::NullWidget
					]
				]
				// 名称和类型
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					NameBox
				]
			]
		];

	// 信息区域
	TooltipContent->AddSlot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Padding(6)
			.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
			[
				InfoBox
			]
		];

	return TooltipContent;
}

void SAssetFavoritesItemRow::AddToToolTipInfoBox(const TSharedRef<SVerticalBox>& InfoBox, const FText& Key, const FText& Value, bool bImportant) const
{
	FSlateColor LabelColor = bImportant ? FSlateColor(FLinearColor(1.0f, 0.5f, 0.0f)) : FSlateColor::UseSubduedForeground();
	FSlateColor ValueColor = bImportant ? FSlateColor(FLinearColor(1.0f, 0.5f, 0.0f)) : FSlateColor::UseForeground();

	InfoBox->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("TooltipKeyFormat", "{0}:  "), Key))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(LabelColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(Value)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(ValueColor)
				.WrapTextAt(400)
			]
		];
}

#undef LOCTEXT_NAMESPACE
