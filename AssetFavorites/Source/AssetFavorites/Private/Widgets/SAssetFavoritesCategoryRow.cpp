// Copyright LoF Team. All Rights Reserved.

#include "SAssetFavoritesCategoryRow.h"
#include "AssetFavoritesDataManager.h"
#include "AssetFavoritesSettings.h"
#include "AssetFavoritesStyle.h"
#include "AssetFavoritesColumnIds.h"
#include "FavoriteItemDragDropOp.h"
#include "FavoriteCategoryDragDropOp.h"
#include "AssetFavoritesTransfer.h"
#include "AssetFavoritesPathImport.h"

#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ContentBrowserDataDragDropOp.h"
#include "Styling/SlateColor.h"

#define LOCTEXT_NAMESPACE "AssetFavoritesCategoryRow"

void SAssetFavoritesCategoryRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FFavoritesTreeNode> InNode)
{
	Node = InNode;
	OwnerTreeViewWeak = InArgs._OwnerTreeView;

	// 构造时通过索引从 DataManager 安全获取 Category 数据
	// 不直接使用 Node->Category 指针，因为它可能已经是悬空指针
	if (Node.IsValid() && Node->CategoryIndex != INDEX_NONE)
	{
		TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
		if (Categories.IsValidIndex(Node->CategoryIndex))
		{
			FFavoritesCategory& Category = Categories[Node->CategoryIndex];
			CachedCategoryId = Category.UniqueId;
			CachedCategoryName = Category.Name;
			CachedCategoryColor = Category.Color;
			CachedConfigSource = Category.ConfigSource;
			CachedItemCount = Category.Items.Num();
			bCategoryDataCached = true;
		}
	}

	SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::Construct(
		FSuperRowType::FArguments()
			.Padding(FMargin(0, 2)),
		InOwnerTable
	);

	// 设置分组行的底色
	SetColorAndOpacity(TAttribute<FLinearColor>::CreateLambda([this]()
	{
		return FLinearColor::White;
	}));
}

const FSlateBrush* SAssetFavoritesCategoryRow::GetBorder() const
{
	// 根据选中/悬停/普通状态返回不同的背景
	if (IsSelected())
	{
		// 选中状态：使用更深的高亮色
		return FAppStyle::GetBrush("Brushes.Select");
	}
	else if (IsHovered() || bIsDragHovering)
	{
		// 悬停状态
		return FAppStyle::GetBrush("Brushes.Hover");
	}
	else
	{
		// 普通状态：分组行使用浅色底色，与普通项区分
		return FAppStyle::GetBrush("Brushes.Header");
	}
}

TSharedRef<SWidget> SAssetFavoritesCategoryRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	// 使用构造时缓存的数据，避免悬空指针问题
	if (!bCategoryDataCached)
	{
		return SNullWidget::NullWidget;
	}

	float UIScale = UAssetFavoritesSettings::Get()->UIScale;
	int32 FontSize = UAssetFavoritesSettings::Get()->FontSize;

	// 名称列 - 显示分类的完整内容
	if (ColumnName == FavoritesColumnId::Name)
	{
		// 根据配置来源选择图标
		const FSlateBrush* SourceIcon = (CachedConfigSource == EFavoritesConfigSource::Shared) 
			? FAppStyle::GetBrush("Icons.Visible")       // 公共：眼睛图标（可见/共享）
			: FAppStyle::GetBrush("Icons.Lock");         // 本地：锁图标（私有/本地存储）
		
		FText SourceTooltip = (CachedConfigSource == EFavoritesConfigSource::Shared)
			? LOCTEXT("SharedCategoryTooltip", "公共分类（会提交到 Git）")
			: LOCTEXT("LocalCategoryTooltip", "本地分类（仅保存在本地）");

		return SNew(SHorizontalBox)
			// 配置来源图标
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(SourceIcon)
				.ColorAndOpacity(CachedConfigSource == EFavoritesConfigSource::Shared 
					? FLinearColor(0.2f, 0.8f, 0.4f)   // 绿色表示公共
					: FLinearColor(0.6f, 0.6f, 0.6f))  // 灰色表示本地
				.DesiredSizeOverride(FVector2D(14 * UIScale, 14 * UIScale))
				.ToolTipText(SourceTooltip)
			]
			// 颜色条
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			.VAlign(VAlign_Fill)
			[
				SNew(SColorBlock)
				.Color(CachedCategoryColor)
				.Size(FVector2D(4 * UIScale, 20 * UIScale))
			]
			// 分类名称
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4, 0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(EditableText, SInlineEditableTextBlock)
				.Text(FText::FromString(CachedCategoryName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", FontSize))
				.ColorAndOpacity(CachedCategoryColor)
				.OnTextCommitted(this, &SAssetFavoritesCategoryRow::OnRenameTextCommitted)
			]
			// 项目数量
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("ItemCount", "({0})"), FText::AsNumber(CachedItemCount)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", FontSize - 2))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
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
				.OnClicked(this, &SAssetFavoritesCategoryRow::OnDeleteClicked)
				.ToolTipText(LOCTEXT("DeleteCategoryTooltip", "删除分类"))
				.ContentPadding(FMargin(2))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.X"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}

	// 其他列留空
	return SNullWidget::NullWidget;
}

FReply SAssetFavoritesCategoryRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		ShowContextMenu(MyGeometry, MouseEvent);
		return FReply::Handled();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// 先调用基类处理选中逻辑（展开/折叠 + TreeView 选中）
		FReply Reply = SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::OnMouseButtonDown(MyGeometry, MouseEvent);
		// 在选中的基础上支持拖拽检测
		return Reply.DetectDrag(AsShared(), EKeys::LeftMouseButton);
	}

	return SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SAssetFavoritesCategoryRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bCategoryDataCached)
	{
		return FReply::Unhandled();
	}

	TSharedRef<FFavoriteCategoryDragDropOp> DragDropOp = FFavoriteCategoryDragDropOp::New(
		CachedCategoryId,
		CachedCategoryName,
		CachedCategoryColor
	);

	return FReply::Handled().BeginDragDrop(DragDropOp);
}

void SAssetFavoritesCategoryRow::ShowContextMenu(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("CategoryActions", LOCTEXT("CategoryActionsSection", "分类操作"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("RenameCategory", "重命名"),
			LOCTEXT("RenameCategoryTooltip", "重命名此分类"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesCategoryRow::OnRenameCategory))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteCategory", "删除"),
			LOCTEXT("DeleteCategoryTooltip", "删除此分类及所有收藏项"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesCategoryRow::OnDeleteCategory))
		);

		// 菜单打开时只读一次剪贴板，避免每帧读取
		const bool bCanPaste = FAssetFavoritesTransfer::ClipboardHasFavorites();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyCategories", "复制"),
			LOCTEXT("CopyCategoriesTooltip", "复制所选分类（含共享/个人标记和全部条目），可粘贴到其他工程的收藏夹"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesCategoryRow::OnCopyCategories))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PasteCategories", "粘贴"),
			LOCTEXT("PasteCategoriesTooltip", "粘贴剪贴板中的分类：同名分类合并，其余新建在此分类之后"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetFavoritesCategoryRow::OnPasteCategories),
				FCanExecuteAction::CreateLambda([bCanPaste]() { return bCanPaste; })
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("BatchAddPaths", "批量添加路径..."),
			LOCTEXT("BatchAddPathsTooltip", "弹窗粘贴多行资产路径（P4 / 磁盘 / /Game）到此分类"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SAssetFavoritesCategoryRow::OnBatchAddPaths))
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("CategoryColor", LOCTEXT("CategoryColorSection", "颜色"));
	{
		// 预设颜色
		TArray<FLinearColor> PresetColors = {
			FLinearColor(0.2f, 0.6f, 1.0f),   // 蓝色
			FLinearColor(0.2f, 0.8f, 0.2f),   // 绿色
			FLinearColor(1.0f, 0.6f, 0.2f),   // 橙色
			FLinearColor(0.8f, 0.2f, 0.2f),   // 红色
			FLinearColor(0.8f, 0.2f, 0.8f),   // 紫色
			FLinearColor(0.9f, 0.9f, 0.2f),   // 黄色
			FLinearColor(0.5f, 0.5f, 0.5f),   // 灰色
			FLinearColor(1.0f, 1.0f, 1.0f),   // 白色
		};

		// 创建一个水平排列的颜色选择器
		TSharedRef<SHorizontalBox> ColorBox = SNew(SHorizontalBox);
		
		for (const FLinearColor& Color : PresetColors)
		{
			ColorBox->AddSlot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SBox)
				.WidthOverride(24.0f)
				.HeightOverride(24.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked_Lambda([this, Color]()
					{
						OnSetCategoryColor(Color);
						FSlateApplication::Get().DismissAllMenus();
						return FReply::Handled();
					})
					[
						SNew(SColorBlock)
						.Color(Color)
						.Size(FVector2D(20.0f, 20.0f))
						.ShowBackgroundForAlpha(true)
						.CornerRadius(FVector4(4.0f, 4.0f, 4.0f, 4.0f))
					]
				]
			];
		}

		MenuBuilder.AddWidget(ColorBox, FText::GetEmpty(), true);
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

void SAssetFavoritesCategoryRow::OnRenameCategory()
{
	EnterEditingMode();
}

void SAssetFavoritesCategoryRow::EnterEditingMode()
{
	if (EditableText.IsValid())
	{
		EditableText->EnterEditingMode();
	}
}

void SAssetFavoritesCategoryRow::OnDeleteCategory()
{
	if (!bCategoryDataCached)
	{
		return;
	}

	// 获取所有选中的项并批量删除
	TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> TreeView = OwnerTreeViewWeak.Pin();
	if (TreeView.IsValid())
	{
		TArray<TSharedPtr<FFavoritesTreeNode>> SelectedItems = TreeView->GetSelectedItems();

		// 如果有多个选中项，批量删除所有选中项
		if (SelectedItems.Num() > 1)
		{
			FAssetFavoritesDataManager::Get().RemoveSelectedItems(SelectedItems);
			return;
		}
	}

	// 单选时直接删除当前分类
	FAssetFavoritesDataManager::Get().RemoveCategory(CachedCategoryId);
}

void SAssetFavoritesCategoryRow::OnSetCategoryColor(FLinearColor NewColor)
{
	if (bCategoryDataCached)
	{
		FAssetFavoritesDataManager::Get().SetCategoryColor(CachedCategoryId, NewColor);
	}
}

TArray<FGuid> SAssetFavoritesCategoryRow::CollectTargetCategoryIds() const
{
	TArray<FGuid> Result;
	if (!bCategoryDataCached)
	{
		return Result;
	}

	TSharedPtr<STreeView<TSharedPtr<FFavoritesTreeNode>>> TreeView = OwnerTreeViewWeak.Pin();
	if (TreeView.IsValid())
	{
		const TArray<TSharedPtr<FFavoritesTreeNode>> Selected = TreeView->GetSelectedItems();

		// 用分类 ID 判断当前行是否在选中集合内（搜索过滤时节点对象会重建，ID 比指针更稳）
		const bool bCurrentInSelection = Selected.ContainsByPredicate([this](const TSharedPtr<FFavoritesTreeNode>& InNode)
		{
			return InNode.IsValid() && InNode->IsCategory() && InNode->CachedCategoryId == CachedCategoryId;
		});

		if (Selected.Num() > 1 && bCurrentInSelection)
		{
			for (const TSharedPtr<FFavoritesTreeNode>& SelNode : Selected)
			{
				if (SelNode.IsValid() && SelNode->IsCategory() && SelNode->CachedCategoryId.IsValid())
				{
					Result.AddUnique(SelNode->CachedCategoryId);
				}
			}
			return Result;
		}
	}

	Result.Add(CachedCategoryId);
	return Result;
}

void SAssetFavoritesCategoryRow::OnCopyCategories()
{
	FAssetFavoritesTransfer::CopyCategories(CollectTargetCategoryIds());
}

void SAssetFavoritesCategoryRow::OnPasteCategories()
{
	if (bCategoryDataCached)
	{
		FAssetFavoritesTransfer::PasteFromClipboard(CachedCategoryId);
	}
}

void SAssetFavoritesCategoryRow::OnBatchAddPaths()
{
	if (bCategoryDataCached)
	{
		FAssetFavoritesPathImport::ShowDialog(CachedCategoryId);
	}
}

void SAssetFavoritesCategoryRow::OnRenameTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
	{
		if (bCategoryDataCached && !NewText.IsEmpty())
		{
			FAssetFavoritesDataManager::Get().RenameCategory(CachedCategoryId, NewText.ToString());
		}
	}
}

FReply SAssetFavoritesCategoryRow::OnDeleteClicked()
{
	OnDeleteCategory();
	return FReply::Handled();
}

FReply SAssetFavoritesCategoryRow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// 检查是否是分组拖拽排序
	TSharedPtr<FFavoriteCategoryDragDropOp> CategoryDragDrop = DragDropEvent.GetOperationAs<FFavoriteCategoryDragDropOp>();
	if (CategoryDragDrop.IsValid())
	{
		// 不能拖到自己
		if (CategoryDragDrop->DraggedCategoryId != CachedCategoryId)
		{
			bIsCategoryDragHovering = true;
			// 计算鼠标位于行的上半部分还是下半部分
			FVector2D LocalPos = MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());
			float HalfHeight = MyGeometry.GetLocalSize().Y / 2.0f;
			bCategoryDragInsertAbove = LocalPos.Y < HalfHeight;
		}
		return FReply::Handled();
	}

	// 资产/收藏项拖入
	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	TSharedPtr<FContentBrowserDataDragDropOp> ContentBrowserDragDrop = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>();
	TSharedPtr<FFavoriteItemDragDropOp> FavoriteItemDragDrop = DragDropEvent.GetOperationAs<FFavoriteItemDragDropOp>();

	if (AssetDragDrop.IsValid() || ContentBrowserDragDrop.IsValid() || FavoriteItemDragDrop.IsValid())
	{
		bIsDragHovering = true;
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAssetFavoritesCategoryRow::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	bIsDragHovering = false;
	bIsCategoryDragHovering = false;
}

FReply SAssetFavoritesCategoryRow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bIsDragHovering = false;
	bIsCategoryDragHovering = false;

	if (!bCategoryDataCached)
	{
		return FReply::Unhandled();
	}

	// 检查分组拖拽排序
	TSharedPtr<FFavoriteCategoryDragDropOp> CategoryDragDrop = DragDropEvent.GetOperationAs<FFavoriteCategoryDragDropOp>();
	if (CategoryDragDrop.IsValid() && CategoryDragDrop->DraggedCategoryId != CachedCategoryId)
	{
		// 在同一个数据源内查找目标位置
		EFavoritesConfigSource DraggedSource = EFavoritesConfigSource::Local;
		EFavoritesConfigSource TargetSource = CachedConfigSource;
		
		// 获取拖拽源的 ConfigSource
		TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
		for (const FFavoritesCategory& Cat : Categories)
		{
			if (Cat.UniqueId == CategoryDragDrop->DraggedCategoryId)
			{
				DraggedSource = Cat.ConfigSource;
				break;
			}
		}
		
		// 只允许在同一数据源内排序（本地之间、公共之间）
		if (DraggedSource == TargetSource)
		{
			// 遍历同源分类，计算目标和源在同源数组中的序号
			int32 SameSourceTargetIdx = 0;
			int32 SameSourceDraggedIdx = 0;
			int32 Counter = 0;
			for (const FFavoritesCategory& Cat : Categories)
			{
				if (Cat.ConfigSource != TargetSource)
				{
					continue;
				}
				if (Cat.UniqueId == CachedCategoryId)
				{
					SameSourceTargetIdx = Counter;
				}
				if (Cat.UniqueId == CategoryDragDrop->DraggedCategoryId)
				{
					SameSourceDraggedIdx = Counter;
				}
				Counter++;
			}
			
			// 计算新位置
			int32 NewPosition = bCategoryDragInsertAbove ? SameSourceTargetIdx : SameSourceTargetIdx + 1;
			// 如果拖拽源在目标前面，移除后目标索引会减1
			if (SameSourceDraggedIdx < SameSourceTargetIdx)
			{
				NewPosition--;
			}
			NewPosition = FMath::Clamp(NewPosition, 0, Counter - 1);
			
			FAssetFavoritesDataManager::Get().MoveCategoryPosition(CategoryDragDrop->DraggedCategoryId, NewPosition);
		}
		
		return FReply::Handled();
	}

	// 检查内部拖拽操作（收藏项在分组间移动）
	TSharedPtr<FFavoriteItemDragDropOp> FavoriteItemDragDrop = DragDropEvent.GetOperationAs<FFavoriteItemDragDropOp>();
	if (FavoriteItemDragDrop.IsValid())
	{
		// 先收集所有要移动的项目信息（SourceCategoryId, ItemId）
		// 因为每次 MoveItem 都会触发 MergeCategoriesData，导致索引失效
		TArray<TPair<FGuid, FGuid>> ItemsToMove;
		
		TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();
		for (const TSharedPtr<FFavoritesTreeNode>& DraggedNode : FavoriteItemDragDrop->DraggedNodes)
		{
			if (DraggedNode->IsItem() && DraggedNode->CategoryIndex != INDEX_NONE && DraggedNode->ItemIndex != INDEX_NONE)
			{
				if (Categories.IsValidIndex(DraggedNode->CategoryIndex))
				{
					FFavoritesCategory& SourceCategory = Categories[DraggedNode->CategoryIndex];
					if (SourceCategory.Items.IsValidIndex(DraggedNode->ItemIndex))
					{
						ItemsToMove.Add(TPair<FGuid, FGuid>(
							SourceCategory.UniqueId,
							SourceCategory.Items[DraggedNode->ItemIndex].UniqueId
						));
					}
				}
			}
		}

		// 批量执行移动
		for (const TPair<FGuid, FGuid>& ItemInfo : ItemsToMove)
		{
			FAssetFavoritesDataManager::Get().MoveItem(ItemInfo.Key, ItemInfo.Value, CachedCategoryId);
		}
		return FReply::Handled();
	}

	// 优先检查 FContentBrowserDataDragDropOp (它继承自 FAssetDragDropOp，支持目录和文件)
	TSharedPtr<FContentBrowserDataDragDropOp> ContentBrowserDragDrop = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>();
	if (ContentBrowserDragDrop.IsValid())
	{
		const TArray<FContentBrowserItem>& DraggedItems = ContentBrowserDragDrop->GetDraggedItems();

		for (const FContentBrowserItem& Item : DraggedItems)
		{
			if (Item.IsFolder())
			{
				FName VirtualPath = Item.GetVirtualPath();
				FAssetFavoritesDataManager::Get().AddItem(CachedCategoryId, VirtualPath.ToString(), EFavoritesItemType::Directory);
			}
			else if (Item.IsFile())
			{
				FAssetData AssetData;
				if (Item.Legacy_TryGetAssetData(AssetData))
				{
					FString AssetPath = AssetData.GetSoftObjectPath().ToString();
					FAssetFavoritesDataManager::Get().AddItem(CachedCategoryId, AssetPath, EFavoritesItemType::Asset);
				}
			}
		}
		return FReply::Handled();
	}

	// 再检查普通的 FAssetDragDropOp (用于从其他地方拖入的资产)
	TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (AssetDragDrop.IsValid())
	{
		for (const FAssetData& Asset : AssetDragDrop->GetAssets())
		{
			FString AssetPath = Asset.GetSoftObjectPath().ToString();
			FAssetFavoritesDataManager::Get().AddItem(CachedCategoryId, AssetPath, EFavoritesItemType::Asset);
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

int32 SAssetFavoritesCategoryRow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 先绘制基类内容
	LayerId = SMultiColumnTableRow<TSharedPtr<FFavoritesTreeNode>>::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// 绘制分组拖拽插入指示线
	if (bIsCategoryDragHovering)
	{
		const float LineThickness = 3.0f;
		const FLinearColor LineColor(1.0f, 0.6f, 0.0f, 1.0f); // 橙色，与收藏项的蓝色区分

		FVector2D LineStart, LineEnd;
		if (bCategoryDragInsertAbove)
		{
			LineStart = FVector2D(0, 0);
			LineEnd = FVector2D(AllottedGeometry.GetLocalSize().X, 0);
		}
		else
		{
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

#undef LOCTEXT_NAMESPACE
