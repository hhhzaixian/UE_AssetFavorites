// Copyright LoF Team. All Rights Reserved.

#include "AssetFavoritesSettings.h"

UAssetFavoritesSettings::UAssetFavoritesSettings()
{
}

UAssetFavoritesSettings* UAssetFavoritesSettings::Get()
{
	return GetMutableDefault<UAssetFavoritesSettings>();
}

#if WITH_EDITOR
void UAssetFavoritesSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnSettingsChanged.Broadcast();
}
#endif
