// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * 样式管理类
 */
class FAssetFavoritesStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static void ReloadTextures();
	
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	/** 图标名称常量 */
	static const FName TabIconName;
	static const FName FolderIconName;
	static const FName AddIconName;
	static const FName DeleteIconName;
	static const FName LocateIconName;
	static const FName NoteIconName;

private:
	static TSharedRef<FSlateStyleSet> Create();
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
