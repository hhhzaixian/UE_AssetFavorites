// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetFavoritesPythonLibrary.generated.h"

/**
 * AssetFavorites Python/Blueprint 接口
 * 暴露收藏夹数据操作给 Python 和蓝图调用
 */
UCLASS()
class UAssetFavoritesPythonLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 添加一个新的收藏分类
	 * @param Name 分类名称
	 * @param bShared 是否为公共分类（提交到Git），默认为本地分类
	 * @param Color 分类颜色（默认蓝色）
	 * @return 新分类的 UniqueId 字符串，失败返回空字符串
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static FString AddCategory(const FString& Name, bool bShared = false, FLinearColor Color = FLinearColor(0.2f, 0.6f, 1.0f));

	/**
	 * 添加一个资产到指定分类
	 * @param CategoryName 分类名称（按名称查找）
	 * @param AssetPath 资产路径（如 /Game/Character/ABP/Player/ABP_LOFPlayer.ABP_LOFPlayer）
	 * @param DisplayName 显示名称（为空时自动从路径提取）
	 * @param Note 备注
	 * @return 是否添加成功
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static bool AddItemToCategory(const FString& CategoryName, const FString& AssetPath, const FString& DisplayName = TEXT(""), const FString& Note = TEXT(""));

	/**
	 * 添加一个目录到指定分类
	 * @param CategoryName 分类名称（按名称查找）
	 * @param DirectoryPath 目录路径（如 /All/Game/Character/Player）
	 * @param DisplayName 显示名称（为空时自动从路径提取）
	 * @param Note 备注
	 * @return 是否添加成功
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static bool AddDirectoryToCategory(const FString& CategoryName, const FString& DirectoryPath, const FString& DisplayName = TEXT(""), const FString& Note = TEXT(""));

	/**
	 * 通过分类 UniqueId 添加资产
	 * @param CategoryId 分类的 UniqueId 字符串
	 * @param AssetPath 资产路径
	 * @param DisplayName 显示名称
	 * @param Note 备注
	 * @return 是否添加成功
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static bool AddItemByCategoryId(const FString& CategoryId, const FString& AssetPath, const FString& DisplayName = TEXT(""), const FString& Note = TEXT(""));

	/**
	 * 设置分类颜色
	 * @param CategoryName 分类名称
	 * @param Color 新颜色
	 * @return 是否设置成功
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static bool SetCategoryColorByName(const FString& CategoryName, FLinearColor Color);

	/**
	 * 获取所有分类名称
	 * @return 分类名称数组
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static TArray<FString> GetAllCategoryNames();

	/**
	 * 移除分类（按名称）
	 * @param CategoryName 分类名称
	 * @return 是否移除成功
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static bool RemoveCategoryByName(const FString& CategoryName);

	/**
	 * 从磁盘重新加载：个人/共享 JSON 有更新才替换内存数据（缺失或损坏时保留内存），
	 * 并刷新全部收藏项的有效状态后通知面板刷新。等同面板空白处右键「重新加载」。
	 */
	UFUNCTION(BlueprintCallable, Category = "AssetFavorites")
	static void ForceReloadFromDisk();
};
