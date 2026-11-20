// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "AssetFavoritesTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/SlateColor.h"

/**
 * 分组拖拽操作 - 用于拖拽重新排列分组顺序
 */
class FFavoriteCategoryDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFavoriteCategoryDragDropOp, FDragDropOperation)

	/** 被拖拽的分类 ID */
	FGuid DraggedCategoryId;

	/** 被拖拽的分类名称（用于显示） */
	FString DraggedCategoryName;

	/** 被拖拽的分类颜色（用于显示） */
	FLinearColor DraggedCategoryColor;

	/** 创建拖拽操作 */
	static TSharedRef<FFavoriteCategoryDragDropOp> New(const FGuid& InCategoryId, const FString& InCategoryName, const FLinearColor& InColor)
	{
		TSharedRef<FFavoriteCategoryDragDropOp> Operation = MakeShareable(new FFavoriteCategoryDragDropOp);
		Operation->DraggedCategoryId = InCategoryId;
		Operation->DraggedCategoryName = InCategoryName;
		Operation->DraggedCategoryColor = InColor;
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.ConnectorFeedback.Border"))
			.Content()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 3, 0)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.DragHandle"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString(DraggedCategoryName))
					.ColorAndOpacity(FSlateColor(DraggedCategoryColor))
				]
			];
	}
};
