// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AssetFavoritesSettings.generated.h"

/**
 * 收藏夹插件设置
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Asset Favorites"))
class UAssetFavoritesSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAssetFavoritesSettings();

	/** 获取设置实例 */
	static UAssetFavoritesSettings* Get();

	/** 字体大小 */
	UPROPERTY(Config, EditAnywhere, Category = "显示", meta = (DisplayName = "字体大小", ClampMin = 8, ClampMax = 24))
	int32 FontSize = 11;

	/** UI 缩放 */
	UPROPERTY(Config, EditAnywhere, Category = "显示", meta = (DisplayName = "UI 缩放", ClampMin = 0.5, ClampMax = 2.0))
	float UIScale = 1.0f;

	/** 显示备注 */
	UPROPERTY(Config, EditAnywhere, Category = "显示", meta = (DisplayName = "显示备注"))
	bool bShowNotes = true;

	/** 显示资产图标 */
	UPROPERTY(Config, EditAnywhere, Category = "显示", meta = (DisplayName = "显示资产图标"))
	bool bShowAssetIcons = true;

	/** 双击打开资产 */
	UPROPERTY(Config, EditAnywhere, Category = "行为", meta = (DisplayName = "双击打开资产"))
	bool bDoubleClickToOpen = true;

	/** 拖拽时自动展开分类 */
	UPROPERTY(Config, EditAnywhere, Category = "行为", meta = (DisplayName = "拖拽时自动展开分类"))
	bool bAutoExpandOnDrag = true;

	/** 隐藏公共收藏组 */
	UPROPERTY(Config, EditAnywhere, Category = "显示", meta = (DisplayName = "隐藏公共收藏组"))
	bool bHideSharedCategories = false;

	// UDeveloperSettings 接口
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FText GetSectionText() const override { return NSLOCTEXT("AssetFavorites", "SettingsSectionText", "Asset Favorites"); }
	virtual FText GetSectionDescription() const override { return NSLOCTEXT("AssetFavorites", "SettingsSectionDesc", "收藏夹插件设置"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** 设置变更委托 */
	DECLARE_MULTICAST_DELEGATE(FOnSettingsChanged);
	FOnSettingsChanged OnSettingsChanged;
};
