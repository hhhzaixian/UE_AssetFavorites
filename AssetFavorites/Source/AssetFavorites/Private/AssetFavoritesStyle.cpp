// Copyright LoF Team. All Rights Reserved.

#include "AssetFavoritesStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Interfaces/IPluginManager.h"

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...) FSlateBoxBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

TSharedPtr<FSlateStyleSet> FAssetFavoritesStyle::StyleInstance = nullptr;

const FName FAssetFavoritesStyle::TabIconName = "AssetFavorites.TabIcon";
const FName FAssetFavoritesStyle::FolderIconName = "AssetFavorites.FolderIcon";
const FName FAssetFavoritesStyle::AddIconName = "AssetFavorites.AddIcon";
const FName FAssetFavoritesStyle::DeleteIconName = "AssetFavorites.DeleteIcon";
const FName FAssetFavoritesStyle::LocateIconName = "AssetFavorites.LocateIcon";
const FName FAssetFavoritesStyle::NoteIconName = "AssetFavorites.NoteIcon";

void FAssetFavoritesStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FAssetFavoritesStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

void FAssetFavoritesStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FAssetFavoritesStyle::Get()
{
	return *StyleInstance;
}

FName FAssetFavoritesStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("AssetFavoritesStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FAssetFavoritesStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("AssetFavorites")->GetBaseDir() / TEXT("Resources"));

	// 使用编辑器内置图标作为默认图标
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	// 注册图标 - 使用 CoreStyle 的默认图标
	Style->Set(TabIconName, new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/icon_tab_Toolbars_16x.png"), Icon16x16));
	Style->Set(FolderIconName, new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/icon_folder_16x.png"), Icon16x16));
	Style->Set(AddIconName, new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/icon_add_40x.png"), Icon16x16));
	Style->Set(DeleteIconName, new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/Cross_12x.png"), Icon16x16));
	Style->Set(LocateIconName, new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/icon_Cascade_Bounds_40x.png"), Icon16x16));
	Style->Set(NoteIconName, new FSlateImageBrush(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/icon_comment_16x.png"), Icon16x16));

	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
