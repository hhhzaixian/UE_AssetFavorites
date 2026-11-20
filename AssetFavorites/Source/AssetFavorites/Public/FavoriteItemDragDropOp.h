// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "AssetFavoritesTypes.h"
#include "AssetFavoritesDataManager.h"
#include "AssetRegistry/AssetRegistryModule.h"

/**
 * 收藏项拖拽操作 - 用于在不同分组间拖拽收藏项
 * 
 * 继承自 FAssetDragDropOp，这样拖拽到 Level Viewport 等其他地方时也能正常工作
 */
class FFavoriteItemDragDropOp : public FAssetDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFavoriteItemDragDropOp, FAssetDragDropOp)

	/** 拖拽的项（AssetFavorites 内部使用） */
	TArray<TSharedPtr<FFavoritesTreeNode>> DraggedNodes;

	/** 检查资产是否可以放置到场景中（不加载资产，仅通过元数据判断） */
	static bool CanPlaceInLevel(const FAssetData& AssetData)
	{
		if (!AssetData.IsValid())
		{
			return false;
		}
		
		// 通过 AssetClassPath 判断资产类型，不调用 GetAsset() 避免触发加载
		FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
		FString ClassPathStr = ClassPath.ToString();
		
		// 蓝图类型：需要检查 ParentClass 标签
		if (ClassPathStr.Contains(TEXT("Blueprint")))
		{
			// 从 AssetData 的 TagsAndValues 中获取 ParentClass
			FAssetTagValueRef ParentClassTag = AssetData.TagsAndValues.FindTag(FName("ParentClass"));
			if (ParentClassTag.IsSet())
			{
				FString ParentClassPath = ParentClassTag.GetValue();
				// 检查父类是否是 Actor 或其子类
				// 常见的可放置父类：Actor, Pawn, Character, etc.
				if (ParentClassPath.Contains(TEXT("Actor")) || 
				    ParentClassPath.Contains(TEXT("Pawn")) || 
				    ParentClassPath.Contains(TEXT("Character")))
				{
					return true;
				}
			}
			// 蓝图但父类不是 Actor，不可放置
			return false;
		}
		
		// 静态网格体 - 可放置
		if (ClassPathStr.Contains(TEXT("StaticMesh")))
		{
			return true;
		}
		
		// 骨骼网格体 - 可放置
		if (ClassPathStr.Contains(TEXT("SkeletalMesh")))
		{
			return true;
		}
		
		// 粒子系统 - 可放置
		if (ClassPathStr.Contains(TEXT("ParticleSystem")) || ClassPathStr.Contains(TEXT("NiagaraSystem")))
		{
			return true;
		}
		
		// 声音 - 可放置
		if (ClassPathStr.Contains(TEXT("SoundCue")) || ClassPathStr.Contains(TEXT("SoundWave")))
		{
			return true;
		}
		
		// 其他常见可放置类型
		if (ClassPathStr.Contains(TEXT("Prefab")) || 
		    ClassPathStr.Contains(TEXT("LevelSequence")))
		{
			return true;
		}
		
		// 默认不可放置（如 DataAsset、Texture、Material 等）
		return false;
	}

	/** 创建拖拽操作 */
	static TSharedRef<FFavoriteItemDragDropOp> New(const TArray<TSharedPtr<FFavoritesTreeNode>>& InDraggedNodes)
	{
		TSharedRef<FFavoriteItemDragDropOp> Operation = MakeShareable(new FFavoriteItemDragDropOp);
		Operation->DraggedNodes = InDraggedNodes;
		
		// 收集有效的资产数据，填充到父类的 AssetData 数组中
		// 这样拖拽到 Level Viewport 等其他地方时也能正常工作
		// 只添加可以放置到场景中的资产，避免引擎断言
		TArray<FAssetData> AssetDataArray;
		
		for (const TSharedPtr<FFavoritesTreeNode>& Node : InDraggedNodes)
		{
			if (Node.IsValid() && Node->IsItem())
			{
				// 只处理资产类型，目录类型不能拖到视口
				if (Node->CachedItemType == EFavoritesItemType::Asset && !Node->CachedItemPath.IsEmpty())
				{
					FAssetData AssetData = FAssetFavoritesDataManager::ResolveAssetData(Node->CachedItemPath);
					// 只添加可以放置到场景中的资产
					if (AssetData.IsValid() && CanPlaceInLevel(AssetData))
					{
						AssetDataArray.Add(AssetData);
					}
				}
			}
		}
		
		// 调用父类的初始化方法
		Operation->Init(MoveTemp(AssetDataArray), TArray<FString>(), nullptr);
		
		// 重要：必须调用 Construct() 完成拖拽操作的初始化
		// 这会触发 InitThumbnail() 等必要的设置
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
					.Text(FText::Format(NSLOCTEXT("AssetFavorites", "DragCount", "移动 {0} 项"), FText::AsNumber(DraggedNodes.Num())))
				]
			];
	}
};
