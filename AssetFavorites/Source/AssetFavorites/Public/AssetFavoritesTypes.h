// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetFavoritesTypes.generated.h"

/**
 * 收藏配置来源
 */
UENUM()
enum class EFavoritesConfigSource : uint8
{
	Local,   // 本地配置（不提交git）
	Shared   // 公共配置（提交git）
};

/**
 * 收藏项类型
 */
UENUM()
enum class EFavoritesItemType : uint8
{
	Asset,      // 资产文件
	Directory   // 目录
};

/**
 * 收藏项数据
 */
USTRUCT()
struct FFavoritesItem
{
	GENERATED_BODY()

	/** 资产路径或目录路径 */
	UPROPERTY()
	FString Path;

	/** 显示名称 */
	UPROPERTY()
	FString DisplayName;

	/** 备注 */
	UPROPERTY()
	FString Note;

	/** 收藏项类型 */
	UPROPERTY()
	EFavoritesItemType ItemType = EFavoritesItemType::Asset;

	/** 排序序号 */
	UPROPERTY()
	int32 SortOrder = 0;

	/** 唯一ID */
	UPROPERTY(Meta = (IgnoreForMemberInitializationTest))
	FGuid UniqueId;

	FFavoritesItem()
	{
		UniqueId = FGuid::NewGuid();
	}

	bool operator==(const FFavoritesItem& Other) const
	{
		return UniqueId == Other.UniqueId;
	}
};

/**
 * 分类数据
 */
USTRUCT()
struct FFavoritesCategory
{
	GENERATED_BODY()

	/** 分类名称 */
	UPROPERTY()
	FString Name;

	/** 颜色标记 */
	UPROPERTY()
	FLinearColor Color = FLinearColor(0.2f, 0.6f, 1.0f); // 默认蓝色

	/** 收藏项列表 */
	UPROPERTY()
	TArray<FFavoritesItem> Items;

	/** 是否展开 */
	UPROPERTY()
	bool bExpanded = true;

	/** 排序序号 */
	UPROPERTY()
	int32 SortOrder = 0;

	/** 唯一ID */
	UPROPERTY(Meta = (IgnoreForMemberInitializationTest))
	FGuid UniqueId;

	/** 配置来源（运行时字段，不序列化） */
	EFavoritesConfigSource ConfigSource = EFavoritesConfigSource::Local;

	FFavoritesCategory()
	{
		UniqueId = FGuid::NewGuid();
	}

	FFavoritesCategory(const FString& InName, EFavoritesConfigSource InSource = EFavoritesConfigSource::Local)
		: Name(InName)
		, ConfigSource(InSource)
	{
		UniqueId = FGuid::NewGuid();
	}

	bool operator==(const FFavoritesCategory& Other) const
	{
		return UniqueId == Other.UniqueId;
	}
};

/**
 * 收藏夹数据根结构
 */
USTRUCT()
struct FFavoritesData
{
	GENERATED_BODY()

	/** 数据版本号 */
	UPROPERTY()
	int32 Version = 1;

	/** 分类列表 */
	UPROPERTY()
	TArray<FFavoritesCategory> Categories;

	/** 分类展开状态映射（仅本地配置使用，Key 为分类 UniqueId 的字符串形式）
	 *  用于存储所有分类（包括共享分类）的展开状态，共享配置不保存 bExpanded */
	UPROPERTY()
	TMap<FString, bool> CategoryExpandedStates;
};

/**
 * 树节点类型 - 用于 TreeView 显示
 */
struct FFavoritesTreeNode : TSharedFromThis<FFavoritesTreeNode>
{
	enum class ENodeType : uint8
	{
		Category,
		Item
	};

	ENodeType NodeType;
	
	// 分类数据（当 NodeType == Category 时有效）
	// 注意：此指针可能在数据刷新后失效，Row Widget 应使用 CategoryIndex 重新获取
	FFavoritesCategory* Category = nullptr;
	
	// 缓存的分类ID（用于展开状态保存，避免指针失效问题）
	FGuid CachedCategoryId;
	
	// 收藏项数据（当 NodeType == Item 时有效）
	// 注意：此指针可能在数据刷新后失效，Row Widget 应使用 CategoryIndex + ItemIndex 重新获取
	FFavoritesItem* Item = nullptr;
	
	// 缓存的 Item 数据（用于拖拽操作和搜索过滤，避免指针失效问题）
	FString CachedItemPath;
	FString CachedDisplayName;
	FString CachedNote;
	EFavoritesItemType CachedItemType = EFavoritesItemType::Asset;
	
	// 分类索引（对 Category 和 Item 节点都有效）
	int32 CategoryIndex = INDEX_NONE;
	
	// 收藏项在分类中的索引（当 NodeType == Item 时有效）
	int32 ItemIndex = INDEX_NONE;

	// 子节点（当 NodeType == Category 时有效）
	TArray<TSharedPtr<FFavoritesTreeNode>> Children;

	// 父节点
	TWeakPtr<FFavoritesTreeNode> Parent;

	bool IsCategory() const { return NodeType == ENodeType::Category; }
	bool IsItem() const { return NodeType == ENodeType::Item; }

	FString GetDisplayName() const
	{
		// 始终使用缓存数据，避免悬空指针风险
		// 裸指针（Category/Item）在数据刷新后可能悬空但不为 null，解引用会崩溃
		return CachedDisplayName;
	}

	FString GetNote() const
	{
		return CachedNote;
	}

	static TSharedPtr<FFavoritesTreeNode> CreateCategoryNode(FFavoritesCategory* InCategory, int32 InCategoryIndex)
	{
		TSharedPtr<FFavoritesTreeNode> Node = MakeShareable(new FFavoritesTreeNode());
		Node->NodeType = ENodeType::Category;
		Node->Category = InCategory;
		Node->CachedCategoryId = InCategory ? InCategory->UniqueId : FGuid();
		Node->CachedDisplayName = InCategory ? InCategory->Name : FString();
		Node->CategoryIndex = InCategoryIndex;
		return Node;
	}

	static TSharedPtr<FFavoritesTreeNode> CreateItemNode(FFavoritesItem* InItem, int32 InCategoryIndex, int32 InItemIndex)
	{
		TSharedPtr<FFavoritesTreeNode> Node = MakeShareable(new FFavoritesTreeNode());
		Node->NodeType = ENodeType::Item;
		Node->Item = InItem;
		// 缓存 Item 数据，用于拖拽和搜索过滤等操作，避免指针失效问题
		if (InItem)
		{
			Node->CachedItemPath = InItem->Path;
			Node->CachedDisplayName = InItem->DisplayName;
			Node->CachedNote = InItem->Note;
			Node->CachedItemType = InItem->ItemType;
		}
		Node->CategoryIndex = InCategoryIndex;
		Node->ItemIndex = InItemIndex;
		return Node;
	}
};
