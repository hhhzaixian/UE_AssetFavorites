// Copyright LoF Team. All Rights Reserved.

#include "AssetFavoritesModule.h"
#include "AssetFavoritesStyle.h"
#include "AssetFavoritesDataManager.h"
#include "AssetFavoritesTypes.h"
#include "AssetFavoritesTransfer.h"
#include "SAssetFavoritesPanel.h"

#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserDelegates.h"

#define LOCTEXT_NAMESPACE "AssetFavorites"

void FAssetFavoritesModule::StartupModule()
{
	// 初始化样式
	FAssetFavoritesStyle::Initialize();
	FAssetFavoritesStyle::ReloadTextures();

	// 注册 Tab
	RegisterTab();

	// 注册 Content Browser 右键菜单扩展
	RegisterContentBrowserExtension();

	// 注册菜单（插入到 Window 菜单最顶部）
	ToolMenusHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		FToolMenuSection& Section = Menu->FindOrAddSection("AssetFavoritesSection",
			FText::GetEmpty(), FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
		Section.AddMenuEntry(
			"AssetFavorites",
			LOCTEXT("AssetFavoritesMenuEntry", "Asset Favorites"),
			LOCTEXT("AssetFavoritesMenuEntryTooltip", "打开资产收藏夹面板"),
			FSlateIcon(FAssetFavoritesStyle::GetStyleSetName(), "AssetFavorites.TabIcon"),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FName(ASSET_FAVORITES_TAB_NAME));
			}))
		);
	}));
}

void FAssetFavoritesModule::ShutdownModule()
{
	// 注销 Asset Registry 回调，避免模块卸载后 DataManager 静态单例仍持有委托
	FAssetFavoritesDataManager::Get().UnbindAssetRegistryEvents();

	// 注销菜单
	if (ToolMenusHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(ToolMenusHandle);
	}

	// 注销 Content Browser 右键菜单扩展
	if (FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
		
		if (AssetContextMenuHandle.IsValid())
		{
			ContentBrowserModule.GetAllAssetViewContextMenuExtenders().RemoveAll(
				[this](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
				{
					return Delegate.GetHandle() == AssetContextMenuHandle;
				}
			);
		}

		if (PathContextMenuHandle.IsValid())
		{
			ContentBrowserModule.GetAllPathViewContextMenuExtenders().RemoveAll(
				[this](const FContentBrowserMenuExtender_SelectedPaths& Delegate)
				{
					return Delegate.GetHandle() == PathContextMenuHandle;
				}
			);
		}
	}

	// 注销 Tab
	UnregisterTab();

	// 清理样式
	FAssetFavoritesStyle::Shutdown();
}

void FAssetFavoritesModule::RegisterTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		FName(ASSET_FAVORITES_TAB_NAME),
		FOnSpawnTab::CreateRaw(this, &FAssetFavoritesModule::SpawnTab))
		.SetDisplayName(LOCTEXT("AssetFavoritesTabTitle", "Asset Favorites"))
		.SetTooltipText(LOCTEXT("AssetFavoritesTabTooltip", "资产收藏夹"))
		.SetIcon(FSlateIcon(FAssetFavoritesStyle::GetStyleSetName(), "AssetFavorites.TabIcon"))
		.SetAutoGenerateMenuEntry(false);
}

void FAssetFavoritesModule::UnregisterTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FName(ASSET_FAVORITES_TAB_NAME));
}

void FAssetFavoritesModule::RegisterContentBrowserExtension()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	// 扩展资产视图右键菜单（资产文件）
	FContentBrowserMenuExtender_SelectedAssets AssetExtenderDelegate = FContentBrowserMenuExtender_SelectedAssets::CreateLambda(
		[](const TArray<FAssetData>& SelectedAssets)
		{
			TSharedRef<FExtender> Extender = MakeShareable(new FExtender());

			if (SelectedAssets.Num() > 0)
			{
				Extender->AddMenuExtension(
					"CommonAssetActions",
					EExtensionHook::After,
					nullptr,
					FMenuExtensionDelegate::CreateLambda([SelectedAssets](FMenuBuilder& MenuBuilder)
					{
						// 加载数据
						FAssetFavoritesDataManager::Get().LoadData();
						TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();

						MenuBuilder.BeginSection("AssetFavorites", LOCTEXT("AssetFavoritesSection", "Asset Favorites"));

						// 添加到收藏子菜单
						MenuBuilder.AddSubMenu(
							LOCTEXT("AddToFavorites", "添加到收藏"),
							LOCTEXT("AddToFavoritesTooltip", "将选中的资产添加到收藏夹"),
							FNewMenuDelegate::CreateLambda([SelectedAssets, &Categories](FMenuBuilder& SubMenuBuilder)
							{
								// 本地分类区域
								SubMenuBuilder.BeginSection("LocalCategories", LOCTEXT("LocalCategoriesSection", "本地收藏"));
								{
									bool bHasLocalCategory = false;
									for (FFavoritesCategory& Category : Categories)
									{
										if (Category.ConfigSource == EFavoritesConfigSource::Local)
										{
											bHasLocalCategory = true;
											FGuid CategoryId = Category.UniqueId;
											SubMenuBuilder.AddMenuEntry(
												FText::FromString(Category.Name),
												FText::Format(LOCTEXT("AddToCategoryTooltip", "添加到分类: {0}"), FText::FromString(Category.Name)),
												FSlateIcon(),
												FUIAction(FExecuteAction::CreateLambda([SelectedAssets, CategoryId]()
												{
													for (const FAssetData& Asset : SelectedAssets)
													{
														FString AssetPath = Asset.GetSoftObjectPath().ToString();
														FAssetFavoritesDataManager::Get().AddItem(CategoryId, AssetPath, EFavoritesItemType::Asset);
													}
												}))
											);
										}
									}
									if (!bHasLocalCategory)
									{
										// 添加到新建本地分类
										SubMenuBuilder.AddMenuEntry(
											LOCTEXT("AddToNewLocalCategory", "+ 新建本地分类"),
											LOCTEXT("AddToNewLocalCategoryTooltip", "创建新的本地分类并添加"),
											FSlateIcon(),
											FUIAction(FExecuteAction::CreateLambda([SelectedAssets]()
											{
												FFavoritesCategory& NewCategory = FAssetFavoritesDataManager::Get().AddCategory(TEXT("New Local Category"), EFavoritesConfigSource::Local);
												for (const FAssetData& Asset : SelectedAssets)
												{
													FString AssetPath = Asset.GetSoftObjectPath().ToString();
													FAssetFavoritesDataManager::Get().AddItem(NewCategory.UniqueId, AssetPath, EFavoritesItemType::Asset);
												}
											}))
										);
									}
								}
								SubMenuBuilder.EndSection();

								// 公共分类区域
								SubMenuBuilder.BeginSection("SharedCategories", LOCTEXT("SharedCategoriesSection", "公共收藏"));
								{
									bool bHasSharedCategory = false;
									for (FFavoritesCategory& Category : Categories)
									{
										if (Category.ConfigSource == EFavoritesConfigSource::Shared)
										{
											bHasSharedCategory = true;
											FGuid CategoryId = Category.UniqueId;
											SubMenuBuilder.AddMenuEntry(
												FText::FromString(Category.Name),
												FText::Format(LOCTEXT("AddToSharedCategoryTooltip", "添加到公共分类: {0}"), FText::FromString(Category.Name)),
												FSlateIcon(),
												FUIAction(FExecuteAction::CreateLambda([SelectedAssets, CategoryId]()
												{
													for (const FAssetData& Asset : SelectedAssets)
													{
														FString AssetPath = Asset.GetSoftObjectPath().ToString();
														FAssetFavoritesDataManager::Get().AddItem(CategoryId, AssetPath, EFavoritesItemType::Asset);
													}
												}))
											);
										}
									}
									if (!bHasSharedCategory)
									{
										// 添加到新建公共分类
										SubMenuBuilder.AddMenuEntry(
											LOCTEXT("AddToNewSharedCategory", "+ 新建公共分类"),
											LOCTEXT("AddToNewSharedCategoryTooltip", "创建新的公共分类并添加（会提交到 Git）"),
											FSlateIcon(),
											FUIAction(FExecuteAction::CreateLambda([SelectedAssets]()
											{
												FFavoritesCategory& NewCategory = FAssetFavoritesDataManager::Get().AddCategory(TEXT("New Shared Category"), EFavoritesConfigSource::Shared);
												for (const FAssetData& Asset : SelectedAssets)
												{
													FString AssetPath = Asset.GetSoftObjectPath().ToString();
													FAssetFavoritesDataManager::Get().AddItem(NewCategory.UniqueId, AssetPath, EFavoritesItemType::Asset);
												}
											}))
										);
									}
								}
								SubMenuBuilder.EndSection();
							}),
							false,
							FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Star")
						);

						MenuBuilder.EndSection();
					})
				);
			}

			return Extender;
		}
	);

	ContentBrowserModule.GetAllAssetViewContextMenuExtenders().Add(AssetExtenderDelegate);
	AssetContextMenuHandle = AssetExtenderDelegate.GetHandle();

	// 扩展路径视图右键菜单（目录）
	FContentBrowserMenuExtender_SelectedPaths PathExtenderDelegate = FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
		[](const TArray<FString>& SelectedPaths)
		{
			TSharedRef<FExtender> Extender = MakeShareable(new FExtender());

			if (SelectedPaths.Num() > 0)
			{
				Extender->AddMenuExtension(
					"PathContextBulkOperations",
					EExtensionHook::After,
					nullptr,
					FMenuExtensionDelegate::CreateLambda([SelectedPaths](FMenuBuilder& MenuBuilder)
					{
						// 加载数据
						FAssetFavoritesDataManager::Get().LoadData();
						TArray<FFavoritesCategory>& Categories = FAssetFavoritesDataManager::Get().GetCategories();

						MenuBuilder.BeginSection("AssetFavorites", LOCTEXT("AssetFavoritesSection", "Asset Favorites"));

						// 添加目录到收藏子菜单
						MenuBuilder.AddSubMenu(
							LOCTEXT("AddFolderToFavorites", "添加目录到收藏"),
							LOCTEXT("AddFolderToFavoritesTooltip", "将选中的目录添加到收藏夹"),
							FNewMenuDelegate::CreateLambda([SelectedPaths, &Categories](FMenuBuilder& SubMenuBuilder)
							{
								// 本地分类区域
								SubMenuBuilder.BeginSection("LocalCategories", LOCTEXT("LocalCategoriesSection", "本地收藏"));
								{
									bool bHasLocalCategory = false;
									for (FFavoritesCategory& Category : Categories)
									{
										if (Category.ConfigSource == EFavoritesConfigSource::Local)
										{
											bHasLocalCategory = true;
											FGuid CategoryId = Category.UniqueId;
											SubMenuBuilder.AddMenuEntry(
												FText::FromString(Category.Name),
												FText::Format(LOCTEXT("AddToCategoryTooltip", "添加到分类: {0}"), FText::FromString(Category.Name)),
												FSlateIcon(),
												FUIAction(FExecuteAction::CreateLambda([SelectedPaths, CategoryId]()
												{
													for (const FString& Path : SelectedPaths)
													{
														FAssetFavoritesDataManager::Get().AddItem(CategoryId, Path, EFavoritesItemType::Directory);
													}
												}))
											);
										}
									}
									if (!bHasLocalCategory)
									{
										SubMenuBuilder.AddMenuEntry(
											LOCTEXT("AddToNewLocalCategory", "+ 新建本地分类"),
											LOCTEXT("AddToNewLocalCategoryTooltip", "创建新的本地分类并添加"),
											FSlateIcon(),
											FUIAction(FExecuteAction::CreateLambda([SelectedPaths]()
											{
												FFavoritesCategory& NewCategory = FAssetFavoritesDataManager::Get().AddCategory(TEXT("New Local Category"), EFavoritesConfigSource::Local);
												for (const FString& Path : SelectedPaths)
												{
													FAssetFavoritesDataManager::Get().AddItem(NewCategory.UniqueId, Path, EFavoritesItemType::Directory);
												}
											}))
										);
									}
								}
								SubMenuBuilder.EndSection();

								// 公共分类区域
								SubMenuBuilder.BeginSection("SharedCategories", LOCTEXT("SharedCategoriesSection", "公共收藏"));
								{
									bool bHasSharedCategory = false;
									for (FFavoritesCategory& Category : Categories)
									{
										if (Category.ConfigSource == EFavoritesConfigSource::Shared)
										{
											bHasSharedCategory = true;
											FGuid CategoryId = Category.UniqueId;
											SubMenuBuilder.AddMenuEntry(
												FText::FromString(Category.Name),
												FText::Format(LOCTEXT("AddToSharedCategoryTooltip", "添加到公共分类: {0}"), FText::FromString(Category.Name)),
												FSlateIcon(),
												FUIAction(FExecuteAction::CreateLambda([SelectedPaths, CategoryId]()
												{
													for (const FString& Path : SelectedPaths)
													{
														FAssetFavoritesDataManager::Get().AddItem(CategoryId, Path, EFavoritesItemType::Directory);
													}
												}))
											);
										}
									}
									if (!bHasSharedCategory)
									{
										SubMenuBuilder.AddMenuEntry(
											LOCTEXT("AddToNewSharedCategory", "+ 新建公共分类"),
											LOCTEXT("AddToNewSharedCategoryTooltip", "创建新的公共分类并添加（会提交到 Git）"),
											FSlateIcon(),
											FUIAction(FExecuteAction::CreateLambda([SelectedPaths]()
											{
												FFavoritesCategory& NewCategory = FAssetFavoritesDataManager::Get().AddCategory(TEXT("New Shared Category"), EFavoritesConfigSource::Shared);
												for (const FString& Path : SelectedPaths)
												{
													FAssetFavoritesDataManager::Get().AddItem(NewCategory.UniqueId, Path, EFavoritesItemType::Directory);
												}
											}))
										);
									}
								}
								SubMenuBuilder.EndSection();
							}),
							false,
							FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Star")
						);

						MenuBuilder.EndSection();
					})
				);
			}

			return Extender;
		}
	);

	ContentBrowserModule.GetAllPathViewContextMenuExtenders().Add(PathExtenderDelegate);
	PathContextMenuHandle = PathExtenderDelegate.GetHandle();
}

TSharedRef<SDockTab> FAssetFavoritesModule::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("AssetFavoritesTabLabel", "Asset Favorites"))
		[
			SNew(SAssetFavoritesPanel)
		];

	// 页签右键菜单：重新加载（检查 JSON 更新 + 刷新条目状态）
	Tab->SetOnExtendContextMenu(SDockTab::FExtendContextMenu::CreateLambda([](FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("AssetFavoritesTabActions", LOCTEXT("TabActionsSection", "Asset Favorites"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("TabReload", "重新加载"),
				LOCTEXT("TabReloadTooltip", "检查个人/共享配置是否被更新，并刷新全部收藏项的资产有效状态"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FAssetFavoritesTransfer::ReloadAll();
				}))
			);
		}
		MenuBuilder.EndSection();
	}));

	return Tab;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetFavoritesModule, AssetFavorites)
