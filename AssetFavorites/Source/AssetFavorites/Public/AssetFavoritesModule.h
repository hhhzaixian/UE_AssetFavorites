// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Tab 标识符 */
#define ASSET_FAVORITES_TAB_NAME TEXT("AssetFavoritesTab")

/**
 * AssetFavorites 模块
 * 提供资产收藏功能的编辑器插件
 */
class FAssetFavoritesModule : public IModuleInterface
{
public:
	/** IModuleInterface 实现 */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 获取模块实例 */
	static FAssetFavoritesModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FAssetFavoritesModule>("AssetFavorites");
	}

	/** 检查模块是否已加载 */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("AssetFavorites");
	}

private:
	/** 注册 NomadTab */
	void RegisterTab();
	
	/** 注销 NomadTab */
	void UnregisterTab();

	/** 注册 Content Browser 右键菜单扩展 */
	void RegisterContentBrowserExtension();

	/** Tab 生成回调 */
	TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);

	/** 菜单注册委托句柄 */
	FDelegateHandle ToolMenusHandle;

	/** Content Browser 资产右键菜单扩展句柄 */
	FDelegateHandle AssetContextMenuHandle;

	/** Content Browser 路径右键菜单扩展句柄 */
	FDelegateHandle PathContextMenuHandle;
};
