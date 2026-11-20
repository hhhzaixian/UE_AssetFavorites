// Copyright LoF Team. All Rights Reserved.

#include "AssetFavoritesPythonLibrary.h"
#include "AssetFavoritesDataManager.h"
#include "AssetFavoritesTypes.h"

FString UAssetFavoritesPythonLibrary::AddCategory(const FString& Name, bool bShared, FLinearColor Color)
{
	FAssetFavoritesDataManager& Mgr = FAssetFavoritesDataManager::Get();
	Mgr.LoadData();

	EFavoritesConfigSource Source = bShared ? EFavoritesConfigSource::Shared : EFavoritesConfigSource::Local;
	FFavoritesCategory& NewCat = Mgr.AddCategory(Name, Source);
	Mgr.SetCategoryColor(NewCat.UniqueId, Color);

	return NewCat.UniqueId.ToString();
}

bool UAssetFavoritesPythonLibrary::AddItemToCategory(const FString& CategoryName, const FString& AssetPath, const FString& DisplayName, const FString& Note)
{
	FAssetFavoritesDataManager& Mgr = FAssetFavoritesDataManager::Get();
	Mgr.LoadData();

	TArray<FFavoritesCategory>& Categories = Mgr.GetCategories();
	for (FFavoritesCategory& Category : Categories)
	{
		if (Category.Name == CategoryName)
		{
			FFavoritesItem* NewItem = Mgr.AddItem(Category.UniqueId, AssetPath, EFavoritesItemType::Asset);
			if (NewItem)
			{
				if (!DisplayName.IsEmpty())
				{
					Mgr.SetItemDisplayName(Category.UniqueId, NewItem->UniqueId, DisplayName);
				}
				if (!Note.IsEmpty())
				{
					Mgr.SetItemNote(Category.UniqueId, NewItem->UniqueId, Note);
				}
				return true;
			}
			return false;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("AssetFavoritesPythonLibrary: Category '%s' not found"), *CategoryName);
	return false;
}

bool UAssetFavoritesPythonLibrary::AddDirectoryToCategory(const FString& CategoryName, const FString& DirectoryPath, const FString& DisplayName, const FString& Note)
{
	FAssetFavoritesDataManager& Mgr = FAssetFavoritesDataManager::Get();
	Mgr.LoadData();

	TArray<FFavoritesCategory>& Categories = Mgr.GetCategories();
	for (FFavoritesCategory& Category : Categories)
	{
		if (Category.Name == CategoryName)
		{
			FFavoritesItem* NewItem = Mgr.AddItem(Category.UniqueId, DirectoryPath, EFavoritesItemType::Directory);
			if (NewItem)
			{
				if (!DisplayName.IsEmpty())
				{
					Mgr.SetItemDisplayName(Category.UniqueId, NewItem->UniqueId, DisplayName);
				}
				if (!Note.IsEmpty())
				{
					Mgr.SetItemNote(Category.UniqueId, NewItem->UniqueId, Note);
				}
				return true;
			}
			return false;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("AssetFavoritesPythonLibrary: Category '%s' not found"), *CategoryName);
	return false;
}

bool UAssetFavoritesPythonLibrary::AddItemByCategoryId(const FString& CategoryId, const FString& AssetPath, const FString& DisplayName, const FString& Note)
{
	FAssetFavoritesDataManager& Mgr = FAssetFavoritesDataManager::Get();
	Mgr.LoadData();

	FGuid Guid;
	if (!FGuid::Parse(CategoryId, Guid))
	{
		UE_LOG(LogTemp, Warning, TEXT("AssetFavoritesPythonLibrary: Invalid CategoryId '%s'"), *CategoryId);
		return false;
	}

	FFavoritesCategory* Category = Mgr.FindCategoryById(Guid);
	if (!Category)
	{
		UE_LOG(LogTemp, Warning, TEXT("AssetFavoritesPythonLibrary: Category with id '%s' not found"), *CategoryId);
		return false;
	}

	FFavoritesItem* NewItem = Mgr.AddItem(Guid, AssetPath, EFavoritesItemType::Asset);
	if (NewItem)
	{
		if (!DisplayName.IsEmpty())
		{
			Mgr.SetItemDisplayName(Guid, NewItem->UniqueId, DisplayName);
		}
		if (!Note.IsEmpty())
		{
			Mgr.SetItemNote(Guid, NewItem->UniqueId, Note);
		}
		return true;
	}
	return false;
}

bool UAssetFavoritesPythonLibrary::SetCategoryColorByName(const FString& CategoryName, FLinearColor Color)
{
	FAssetFavoritesDataManager& Mgr = FAssetFavoritesDataManager::Get();
	Mgr.LoadData();

	TArray<FFavoritesCategory>& Categories = Mgr.GetCategories();
	for (FFavoritesCategory& Category : Categories)
	{
		if (Category.Name == CategoryName)
		{
			return Mgr.SetCategoryColor(Category.UniqueId, Color);
		}
	}
	return false;
}

TArray<FString> UAssetFavoritesPythonLibrary::GetAllCategoryNames()
{
	FAssetFavoritesDataManager& Mgr = FAssetFavoritesDataManager::Get();
	Mgr.LoadData();

	TArray<FString> Names;
	for (const FFavoritesCategory& Category : Mgr.GetCategories())
	{
		Names.Add(Category.Name);
	}
	return Names;
}

bool UAssetFavoritesPythonLibrary::RemoveCategoryByName(const FString& CategoryName)
{
	FAssetFavoritesDataManager& Mgr = FAssetFavoritesDataManager::Get();
	Mgr.LoadData();

	TArray<FFavoritesCategory>& Categories = Mgr.GetCategories();
	for (FFavoritesCategory& Category : Categories)
	{
		if (Category.Name == CategoryName)
		{
			return Mgr.RemoveCategory(Category.UniqueId);
		}
	}
	return false;
}

void UAssetFavoritesPythonLibrary::ForceReloadFromDisk()
{
	// 真正重读磁盘：JSON 有更新才替换内存数据（缺失或损坏时保留内存），并刷新全部条目状态后广播刷新
	const FFavoritesReloadReport Report = FAssetFavoritesDataManager::Get().ReloadAll();
	UE_LOG(LogTemp, Log, TEXT("AssetFavoritesPythonLibrary: ForceReloadFromDisk - local reloaded=%d, shared reloaded=%d, valid=%d, moved=%d, invalid=%d"),
		Report.bLocalReloaded, Report.bSharedReloaded, Report.ValidCount, Report.MovedCount, Report.InvalidCount);
}
