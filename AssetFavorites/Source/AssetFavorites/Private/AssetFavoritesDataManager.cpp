// Copyright LoF Team. All Rights Reserved.

#include "AssetFavoritesDataManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetFavoritesSourceControlUtils.h"
#include "Misc/PackageName.h"

// 调试日志开关：设为 1 开启调试日志，设为 0 关闭
#define ASSET_FAVORITES_DEBUG_LOG 0

#if ASSET_FAVORITES_DEBUG_LOG
	#define AF_LOG(Format, ...) UE_LOG(LogTemp, Log, TEXT("AssetFavorites ") Format, ##__VA_ARGS__)
#else
	#define AF_LOG(Format, ...) 
#endif

FAssetFavoritesDataManager& FAssetFavoritesDataManager::Get()
{
	static FAssetFavoritesDataManager Instance;
	return Instance;
}

FAssetFavoritesDataManager::FAssetFavoritesDataManager()
{
}

FAssetFavoritesDataManager::~FAssetFavoritesDataManager()
{
	UnbindAssetRegistryEvents();
}

FString FAssetFavoritesDataManager::GetLocalDataFilePath() const
{
	// 本地配置放在 Saved 目录下（被 .gitignore 忽略）
	return FPaths::ProjectSavedDir() / TEXT("Config") / TEXT("AssetFavorites_Local.json");
}

FString FAssetFavoritesDataManager::GetSharedDataFilePath() const
{
	// 公共配置放在 Config 目录下（会提交到 git）
	return FPaths::ProjectConfigDir() / TEXT("AssetFavorites_Shared.json");
}

void FAssetFavoritesDataManager::LoadData()
{
	if (bIsLoaded)
	{
		return;
	}

	// 检查是否需要迁移旧配置
	FString OldConfigPath = FPaths::ProjectSavedDir() / TEXT("Config") / TEXT("AssetFavorites.json");
	FString NewLocalConfigPath = GetLocalDataFilePath();
	
	if (FPaths::FileExists(OldConfigPath) && !FPaths::FileExists(NewLocalConfigPath))
	{
		// 旧配置存在且新配置不存在，迁移旧配置到新的本地配置
		UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Migrating old config from %s to %s"), *OldConfigPath, *NewLocalConfigPath);
		
		FString Directory = FPaths::GetPath(NewLocalConfigPath);
		IFileManager::Get().MakeDirectory(*Directory, true);
		IFileManager::Get().Move(*NewLocalConfigPath, *OldConfigPath, true, true);
	}

	// 先记录文件状态再读取，避免读取期间的外部写入被漏掉
	LocalFileStamp = QueryFileStamp(GetLocalDataFilePath());
	SharedFileStamp = QueryFileStamp(GetSharedDataFilePath());

	// 加载本地配置
	LoadDataFromFile(GetLocalDataFilePath(), EFavoritesConfigSource::Local, LocalData);
	
	// 加载公共配置
	LoadDataFromFile(GetSharedDataFilePath(), EFavoritesConfigSource::Shared, SharedData);

	// 合并数据
	MergeCategoriesData();

	bIsLoaded = true;
	BindAssetRegistryEvents();
	RepairMovedItemPaths();
	UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Loaded %d local categories, %d shared categories"), 
		LocalData.Categories.Num(), SharedData.Categories.Num());
}

FAssetFavoritesDataManager::FFileStamp FAssetFavoritesDataManager::QueryFileStamp(const FString& FilePath)
{
	FFileStamp Stamp;
	Stamp.Size = IFileManager::Get().FileSize(*FilePath);
	if (Stamp.Size >= 0)
	{
		Stamp.Time = IFileManager::Get().GetTimeStamp(*FilePath);
	}
	return Stamp;
}

void FAssetFavoritesDataManager::RecordFileStamp(EFavoritesConfigSource Source)
{
	if (Source == EFavoritesConfigSource::Local)
	{
		LocalFileStamp = QueryFileStamp(GetLocalDataFilePath());
	}
	else
	{
		SharedFileStamp = QueryFileStamp(GetSharedDataFilePath());
	}
}

void FAssetFavoritesDataManager::LoadDataFromFile(const FString& FilePath, EFavoritesConfigSource Source, FFavoritesData& OutData)
{
	OutData.Categories.Empty();
	OutData.CategoryExpandedStates.Empty();
	OutData.Version = 1;

	FFavoritesData Parsed;
	if (ReadDataFile(FilePath, Source, Parsed) == EFavoritesReadResult::Ok)
	{
		OutData = MoveTemp(Parsed);
	}
}

FAssetFavoritesDataManager::EFavoritesReadResult FAssetFavoritesDataManager::ReadDataFile(const FString& FilePath, EFavoritesConfigSource Source, FFavoritesData& FinalOut)
{
	// 解析到临时对象，成功后才写入 FinalOut，保证失败时不破坏调用方已有数据
	FFavoritesData OutData;
	OutData.Version = 1;

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return EFavoritesReadResult::Missing;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return EFavoritesReadResult::ParseFailed;
	}

	// 读取版本号
	OutData.Version = JsonObject->GetIntegerField(TEXT("Version"));

	// 读取分类展开状态映射（仅本地配置）
	if (Source == EFavoritesConfigSource::Local)
	{
		const TSharedPtr<FJsonObject>* ExpandedStatesObject;
		if (JsonObject->TryGetObjectField(TEXT("CategoryExpandedStates"), ExpandedStatesObject))
		{
			for (const auto& Pair : (*ExpandedStatesObject)->Values)
			{
				OutData.CategoryExpandedStates.Add(FString(Pair.Key), Pair.Value->AsBool());
			}
		}
	}

	// 读取分类数组
	const TArray<TSharedPtr<FJsonValue>>* CategoriesArray;
	if (!JsonObject->TryGetArrayField(TEXT("Categories"), CategoriesArray))
	{
		FinalOut = MoveTemp(OutData);
		return EFavoritesReadResult::Ok;
	}

	for (const TSharedPtr<FJsonValue>& CategoryValue : *CategoriesArray)
	{
		const TSharedPtr<FJsonObject>* CategoryObject;
		if (!CategoryValue->TryGetObject(CategoryObject))
		{
			continue;
		}

		// 名称、排序、UniqueId、颜色、收藏项统一由 CategoryFromJson 解析
		FFavoritesCategory Category;
		CategoryFromJson(*CategoryObject, Category);
		Category.ConfigSource = Source;

		// 展开状态：本地分类从 JSON 直接读取（兼容旧格式），共享分类默认展开
		if (Source == EFavoritesConfigSource::Local)
		{
			// 本地分类：先尝试从 CategoryExpandedStates 读取，否则从分类自身读取（兼容旧格式）
			if (const bool* ExpandedState = OutData.CategoryExpandedStates.Find(Category.UniqueId.ToString()))
			{
				Category.bExpanded = *ExpandedState;
			}
			else
			{
				Category.bExpanded = (*CategoryObject)->GetBoolField(TEXT("bExpanded"));
			}
		}
		else
		{
			// 共享分类：默认展开，实际状态从本地配置的 CategoryExpandedStates 恢复
			Category.bExpanded = true;
		}

		OutData.Categories.Add(Category);
	}

	FinalOut = MoveTemp(OutData);
	return EFavoritesReadResult::Ok;
}

TSharedRef<FJsonObject> FAssetFavoritesDataManager::CategoryToJson(const FFavoritesCategory& Category, bool bIncludeExpanded)
{
	TSharedRef<FJsonObject> CategoryObject = MakeShared<FJsonObject>();
	CategoryObject->SetStringField(TEXT("Name"), Category.Name);
	// 共享配置不保存 bExpanded，本地配置仍保存（兼容）
	if (bIncludeExpanded)
	{
		CategoryObject->SetBoolField(TEXT("bExpanded"), Category.bExpanded);
	}
	CategoryObject->SetNumberField(TEXT("SortOrder"), Category.SortOrder);
	CategoryObject->SetStringField(TEXT("UniqueId"), Category.UniqueId.ToString());

	// 保存颜色
	TSharedRef<FJsonObject> ColorObject = MakeShared<FJsonObject>();
	ColorObject->SetNumberField(TEXT("R"), Category.Color.R);
	ColorObject->SetNumberField(TEXT("G"), Category.Color.G);
	ColorObject->SetNumberField(TEXT("B"), Category.Color.B);
	ColorObject->SetNumberField(TEXT("A"), Category.Color.A);
	CategoryObject->SetObjectField(TEXT("Color"), ColorObject);

	// 保存收藏项数组
	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	for (const FFavoritesItem& Item : Category.Items)
	{
		TSharedRef<FJsonObject> ItemObject = MakeShared<FJsonObject>();
		ItemObject->SetStringField(TEXT("Path"), Item.Path);
		ItemObject->SetStringField(TEXT("DisplayName"), Item.DisplayName);
		ItemObject->SetStringField(TEXT("Note"), Item.Note);
		ItemObject->SetNumberField(TEXT("ItemType"), static_cast<int32>(Item.ItemType));
		ItemObject->SetNumberField(TEXT("SortOrder"), Item.SortOrder);
		ItemObject->SetStringField(TEXT("UniqueId"), Item.UniqueId.ToString());

		ItemsArray.Add(MakeShared<FJsonValueObject>(ItemObject));
	}
	CategoryObject->SetArrayField(TEXT("Items"), ItemsArray);

	return CategoryObject;
}

bool FAssetFavoritesDataManager::CategoryFromJson(const TSharedPtr<FJsonObject>& CategoryObject, FFavoritesCategory& OutCategory)
{
	if (!CategoryObject.IsValid())
	{
		return false;
	}

	OutCategory.Name = CategoryObject->GetStringField(TEXT("Name"));
	OutCategory.SortOrder = CategoryObject->GetIntegerField(TEXT("SortOrder"));

	// UniqueId 缺失或非法时保留 OutCategory 构造时生成的 GUID
	FString GuidStr;
	FGuid ParsedGuid;
	if (CategoryObject->TryGetStringField(TEXT("UniqueId"), GuidStr) && FGuid::Parse(GuidStr, ParsedGuid) && ParsedGuid.IsValid())
	{
		OutCategory.UniqueId = ParsedGuid;
	}

	// 读取颜色
	const TSharedPtr<FJsonObject>* ColorObject;
	if (CategoryObject->TryGetObjectField(TEXT("Color"), ColorObject))
	{
		OutCategory.Color.R = (*ColorObject)->GetNumberField(TEXT("R"));
		OutCategory.Color.G = (*ColorObject)->GetNumberField(TEXT("G"));
		OutCategory.Color.B = (*ColorObject)->GetNumberField(TEXT("B"));
		OutCategory.Color.A = (*ColorObject)->GetNumberField(TEXT("A"));
	}

	// 读取收藏项数组
	OutCategory.Items.Reset();
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray;
	if (CategoryObject->TryGetArrayField(TEXT("Items"), ItemsArray))
	{
		for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
		{
			const TSharedPtr<FJsonObject>* ItemObject;
			if (!ItemValue->TryGetObject(ItemObject))
			{
				continue;
			}

			FFavoritesItem Item;
			Item.Path = (*ItemObject)->GetStringField(TEXT("Path"));
			Item.DisplayName = (*ItemObject)->GetStringField(TEXT("DisplayName"));
			Item.Note = (*ItemObject)->GetStringField(TEXT("Note"));
			Item.ItemType = static_cast<EFavoritesItemType>((*ItemObject)->GetIntegerField(TEXT("ItemType")));
			Item.SortOrder = (*ItemObject)->GetIntegerField(TEXT("SortOrder"));

			FString ItemGuidStr;
			FGuid ParsedItemGuid;
			if ((*ItemObject)->TryGetStringField(TEXT("UniqueId"), ItemGuidStr) && FGuid::Parse(ItemGuidStr, ParsedItemGuid) && ParsedItemGuid.IsValid())
			{
				Item.UniqueId = ParsedItemGuid;
			}

			OutCategory.Items.Add(Item);
		}
	}

	return true;
}

void FAssetFavoritesDataManager::SaveData()
{
	// 收集所有分类（包括共享分类）的展开状态到本地配置的映射中
	LocalData.CategoryExpandedStates.Empty();
	for (const FFavoritesCategory& Category : MergedCategories)
	{
		LocalData.CategoryExpandedStates.Add(Category.UniqueId.ToString(), Category.bExpanded);
	}

	// 分别保存本地和公共配置
	SaveDataToFile(GetLocalDataFilePath(), LocalData, EFavoritesConfigSource::Local);
	SaveDataToFile(GetSharedDataFilePath(), SharedData, EFavoritesConfigSource::Shared);
	
	UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Saved %d local categories to %s, %d shared categories to %s"), 
		LocalData.Categories.Num(), *GetLocalDataFilePath(),
		SharedData.Categories.Num(), *GetSharedDataFilePath());
}

void FAssetFavoritesDataManager::SaveDataToFile(const FString& FilePath, const FFavoritesData& InData, EFavoritesConfigSource Source)
{
	TSharedRef<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetNumberField(TEXT("Version"), InData.Version);

	// 本地配置保存分类展开状态映射
	if (Source == EFavoritesConfigSource::Local && InData.CategoryExpandedStates.Num() > 0)
	{
		TSharedPtr<FJsonObject> ExpandedStatesObject = MakeShareable(new FJsonObject);
		for (const auto& Pair : InData.CategoryExpandedStates)
		{
			ExpandedStatesObject->SetBoolField(Pair.Key, Pair.Value);
		}
		JsonObject->SetObjectField(TEXT("CategoryExpandedStates"), ExpandedStatesObject);
	}

	TArray<TSharedPtr<FJsonValue>> CategoriesArray;
	for (const FFavoritesCategory& Category : InData.Categories)
	{
		// 共享配置不保存 bExpanded，本地配置仍保存（兼容）
		CategoriesArray.Add(MakeShared<FJsonValueObject>(CategoryToJson(Category, Source == EFavoritesConfigSource::Local)));
	}
	JsonObject->SetArrayField(TEXT("Categories"), CategoriesArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject, Writer);

	FString Directory = FPaths::GetPath(FilePath);
	
	// 确保目录存在
	IFileManager::Get().MakeDirectory(*Directory, true);
	
	// 检查文件是否只读，尝试 checkout 或去除只读属性
	if (!FAssetFavoritesSourceControlUtils::EnsureFileWritable(FilePath))
	{
		// 仍然无法写入，显示警告通知
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("AssetFavorites", "ReadOnlyWarning", "无法保存公共收藏配置：文件 {0} 是只读的。\n请先 checkout 该文件。"),
			FText::FromString(FPaths::GetCleanFilename(FilePath))
		));
		Info.ExpireDuration = 5.0f;
		Info.bUseThrobber = false;
		Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.Warning"));
		FSlateNotificationManager::Get().AddNotification(Info);
		
		UE_LOG(LogTemp, Warning, TEXT("AssetFavorites: Cannot save to read-only file: %s"), *FilePath);
		return;
	}
	
	if (FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		// 记录自己写入后的文件状态，避免 Reload 时把自己的写入当成外部修改
		RecordFileStamp(Source);
	}
}

void FAssetFavoritesDataManager::MergeCategoriesData()
{
	MergedCategories.Empty();

	// 先添加公共分类（显示在上方）
	for (FFavoritesCategory& Category : SharedData.Categories)
	{
		Category.ConfigSource = EFavoritesConfigSource::Shared;
		// 从本地配置的展开状态映射中恢复共享分类的展开状态
		if (const bool* ExpandedState = LocalData.CategoryExpandedStates.Find(Category.UniqueId.ToString()))
		{
			Category.bExpanded = *ExpandedState;
		}
		AF_LOG("Merge: SharedCategory '%s' bExpanded=%d", *Category.Name, Category.bExpanded);
		MergedCategories.Add(Category);
	}

	// 再添加本地分类（显示在下方）
	for (FFavoritesCategory& Category : LocalData.Categories)
	{
		Category.ConfigSource = EFavoritesConfigSource::Local;
		// 本地分类也从映射中恢复（如果有）
		if (const bool* ExpandedState = LocalData.CategoryExpandedStates.Find(Category.UniqueId.ToString()))
		{
			Category.bExpanded = *ExpandedState;
		}
		AF_LOG("Merge: LocalCategory '%s' bExpanded=%d", *Category.Name, Category.bExpanded);
		MergedCategories.Add(Category);
	}

	// 按 SortOrder 排序（公共和本地分开排序，公共在前）
	MergedCategories.Sort([](const FFavoritesCategory& A, const FFavoritesCategory& B)
	{
		if (A.ConfigSource != B.ConfigSource)
		{
			return A.ConfigSource == EFavoritesConfigSource::Shared;
		}
		return A.SortOrder < B.SortOrder;
	});
}

EFavoritesConfigSource FAssetFavoritesDataManager::GetCategorySource(const FGuid& CategoryId) const
{
	for (const FFavoritesCategory& Category : LocalData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			return EFavoritesConfigSource::Local;
		}
	}
	return EFavoritesConfigSource::Shared;
}

FFavoritesCategory& FAssetFavoritesDataManager::AddCategory(const FString& Name, EFavoritesConfigSource Source)
{
	FFavoritesCategory NewCategory(Name, Source);
	
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;
	NewCategory.SortOrder = TargetData.Categories.Num();
	TargetData.Categories.Add(NewCategory);
	
	// 重新合并
	MergeCategoriesData();
	
	SaveData();
	OnDataChanged.Broadcast();
	
	// 返回合并列表中的引用
	return *FindCategoryById(NewCategory.UniqueId);
}

bool FAssetFavoritesDataManager::RemoveCategory(const FGuid& CategoryId)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	int32 Index = TargetData.Categories.IndexOfByPredicate([&CategoryId](const FFavoritesCategory& Cat)
	{
		return Cat.UniqueId == CategoryId;
	});

	if (Index != INDEX_NONE)
	{
		TargetData.Categories.RemoveAt(Index);
		MergeCategoriesData();
		SaveData();
		OnDataChanged.Broadcast();
		return true;
	}
	return false;
}

bool FAssetFavoritesDataManager::RenameCategory(const FGuid& CategoryId, const FString& NewName)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	for (FFavoritesCategory& Category : TargetData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			Category.Name = NewName;
			MergeCategoriesData();
			SaveData();
			OnDataChanged.Broadcast();
			return true;
		}
	}
	return false;
}

bool FAssetFavoritesDataManager::SetCategoryColor(const FGuid& CategoryId, const FLinearColor& Color)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	for (FFavoritesCategory& Category : TargetData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			Category.Color = Color;
			MergeCategoriesData();
			SaveData();
			OnDataChanged.Broadcast();
			return true;
		}
	}
	return false;
}

bool FAssetFavoritesDataManager::SetCategoryExpanded(const FGuid& CategoryId, bool bExpanded)
{
	// 展开状态统一存储到本地配置的映射中，不修改共享配置的源数据
	FString CategoryIdStr = CategoryId.ToString();
	AF_LOG("SetCategoryExpanded: CategoryId='%s' NewExpanded=%d", *CategoryIdStr, bExpanded);
	
	LocalData.CategoryExpandedStates.Add(CategoryIdStr, bExpanded);
	
	// 同时更新源数据中的状态（用于 MergeCategoriesData 后立即生效）
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	for (FFavoritesCategory& Category : TargetData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			Category.bExpanded = bExpanded;
			break;
		}
	}
	
	MergeCategoriesData();
	SaveData();
	// 展开状态变更不需要广播完整刷新
	return true;
}

FFavoritesItem* FAssetFavoritesDataManager::AddItem(const FGuid& CategoryId, const FString& Path, EFavoritesItemType ItemType)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	FFavoritesCategory* Category = nullptr;
	for (FFavoritesCategory& Cat : TargetData.Categories)
	{
		if (Cat.UniqueId == CategoryId)
		{
			Category = &Cat;
			break;
		}
	}

	if (!Category)
	{
		return nullptr;
	}

	// 检查是否已存在
	for (const FFavoritesItem& ExistingItem : Category->Items)
	{
		if (ExistingItem.Path == Path)
		{
			return nullptr; // 已存在
		}
	}

	FFavoritesItem NewItem;
	NewItem.Path = Path;
	NewItem.ItemType = ItemType;
	NewItem.SortOrder = Category->Items.Num();

	// 提取显示名称
	if (ItemType == EFavoritesItemType::Directory)
	{
		NewItem.DisplayName = FPaths::GetCleanFilename(Path);
	}
	else
	{
		NewItem.DisplayName = FPaths::GetBaseFilename(Path);
	}

	Category->Items.Add(NewItem);
	MergeCategoriesData();
	SaveData();
	OnDataChanged.Broadcast();

	// 从合并列表中找到并返回
	FFavoritesCategory* MergedCategory = FindCategoryById(CategoryId);
	if (MergedCategory && MergedCategory->Items.Num() > 0)
	{
		return &MergedCategory->Items.Last();
	}
	return nullptr;
}

FFavoritesAddItemsResult FAssetFavoritesDataManager::AddItems(const FGuid& CategoryId, const TArray<TPair<FString, EFavoritesItemType>>& InItems)
{
	FFavoritesAddItemsResult Result;
	if (InItems.Num() == 0)
	{
		return Result;
	}

	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	FFavoritesCategory* Category = nullptr;
	for (FFavoritesCategory& Cat : TargetData.Categories)
	{
		if (Cat.UniqueId == CategoryId)
		{
			Category = &Cat;
			break;
		}
	}

	if (!Category)
	{
		return Result;
	}

	TSet<FString> ExistingPaths;
	ExistingPaths.Reserve(Category->Items.Num() + InItems.Num());
	for (const FFavoritesItem& ExistingItem : Category->Items)
	{
		ExistingPaths.Add(ExistingItem.Path);
	}

	for (const TPair<FString, EFavoritesItemType>& Pair : InItems)
	{
		if (Pair.Key.IsEmpty())
		{
			continue;
		}
		if (ExistingPaths.Contains(Pair.Key))
		{
			++Result.SkippedDuplicate;
			continue;
		}

		FFavoritesItem NewItem;
		NewItem.Path = Pair.Key;
		NewItem.ItemType = Pair.Value;
		NewItem.SortOrder = Category->Items.Num();
		if (Pair.Value == EFavoritesItemType::Directory)
		{
			NewItem.DisplayName = FPaths::GetCleanFilename(Pair.Key);
		}
		else
		{
			NewItem.DisplayName = FPaths::GetBaseFilename(Pair.Key);
		}

		Category->Items.Add(MoveTemp(NewItem));
		ExistingPaths.Add(Pair.Key);
		++Result.Added;
	}

	if (Result.Added > 0)
	{
		MergeCategoriesData();
		SaveData();
		OnDataChanged.Broadcast();
	}

	return Result;
}

bool FAssetFavoritesDataManager::RemoveItem(const FGuid& CategoryId, const FGuid& ItemId)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	// 调试：打印当前所有分类的 bExpanded 状态
	AF_LOG("RemoveItem: Before remove, LocalData categories:");
	for (const FFavoritesCategory& Cat : LocalData.Categories)
	{
		AF_LOG("  - '%s' bExpanded=%d", *Cat.Name, Cat.bExpanded);
	}

	for (FFavoritesCategory& Category : TargetData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			int32 Index = Category.Items.IndexOfByPredicate([&ItemId](const FFavoritesItem& Item)
			{
				return Item.UniqueId == ItemId;
			});

			if (Index != INDEX_NONE)
			{
				Category.Items.RemoveAt(Index);
				MergeCategoriesData();
				SaveData();
				OnDataChanged.Broadcast();
				return true;
			}
			break;
		}
	}
	return false;
}

void FAssetFavoritesDataManager::RemoveSelectedItems(const TArray<TSharedPtr<FFavoritesTreeNode>>& SelectedNodes)
{
	if (SelectedNodes.Num() == 0)
	{
		return;
	}

	// 分别收集要删除的分组和收藏项
	TArray<FGuid> CategoriesToRemove;
	TArray<TPair<FGuid, FGuid>> ItemsToRemove;

	for (const TSharedPtr<FFavoritesTreeNode>& Node : SelectedNodes)
	{
		if (Node->IsCategory())
		{
			CategoriesToRemove.Add(Node->CachedCategoryId);
		}
		else if (Node->IsItem() && Node->CategoryIndex != INDEX_NONE && Node->ItemIndex != INDEX_NONE)
		{
			if (MergedCategories.IsValidIndex(Node->CategoryIndex))
			{
				FFavoritesCategory& Category = MergedCategories[Node->CategoryIndex];
				// 如果该 Item 所属的分组已经在待删除列表中，则跳过（删除分组会连带删除其所有 Item）
				if (CategoriesToRemove.Contains(Category.UniqueId))
				{
					continue;
				}
				if (Category.Items.IsValidIndex(Node->ItemIndex))
				{
					ItemsToRemove.Add(TPair<FGuid, FGuid>(
						Category.UniqueId,
						Category.Items[Node->ItemIndex].UniqueId
					));
				}
			}
		}
	}

	// 先批量删除收藏项（因为删除分组会连带删除其下所有项）
	for (const TPair<FGuid, FGuid>& ItemInfo : ItemsToRemove)
	{
		RemoveItem(ItemInfo.Key, ItemInfo.Value);
	}

	// 再批量删除分组
	for (const FGuid& CategoryId : CategoriesToRemove)
	{
		RemoveCategory(CategoryId);
	}
}

bool FAssetFavoritesDataManager::SetItemNote(const FGuid& CategoryId, const FGuid& ItemId, const FString& Note)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	for (FFavoritesCategory& Category : TargetData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			for (FFavoritesItem& Item : Category.Items)
			{
				if (Item.UniqueId == ItemId)
				{
					Item.Note = Note;
					MergeCategoriesData();
					SaveData();
					OnDataChanged.Broadcast();
					return true;
				}
			}
			break;
		}
	}
	return false;
}

bool FAssetFavoritesDataManager::SetItemDisplayName(const FGuid& CategoryId, const FGuid& ItemId, const FString& DisplayName)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	for (FFavoritesCategory& Category : TargetData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			for (FFavoritesItem& Item : Category.Items)
			{
				if (Item.UniqueId == ItemId)
				{
					Item.DisplayName = DisplayName;
					MergeCategoriesData();
					SaveData();
					OnDataChanged.Broadcast();
					return true;
				}
			}
			break;
		}
	}
	return false;
}

bool FAssetFavoritesDataManager::MoveItem(const FGuid& SourceCategoryId, const FGuid& ItemId, const FGuid& TargetCategoryId)
{
	EFavoritesConfigSource SourceSource = GetCategorySource(SourceCategoryId);
	EFavoritesConfigSource TargetSource = GetCategorySource(TargetCategoryId);

	FFavoritesData& SourceData = (SourceSource == EFavoritesConfigSource::Local) ? LocalData : SharedData;
	FFavoritesData& TargetData = (TargetSource == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	FFavoritesCategory* SourceCategory = nullptr;
	FFavoritesCategory* TargetCategory = nullptr;

	for (FFavoritesCategory& Cat : SourceData.Categories)
	{
		if (Cat.UniqueId == SourceCategoryId)
		{
			SourceCategory = &Cat;
			break;
		}
	}

	for (FFavoritesCategory& Cat : TargetData.Categories)
	{
		if (Cat.UniqueId == TargetCategoryId)
		{
			TargetCategory = &Cat;
			break;
		}
	}
	
	if (!SourceCategory || !TargetCategory)
	{
		return false;
	}

	int32 ItemIndex = SourceCategory->Items.IndexOfByPredicate([&ItemId](const FFavoritesItem& Item)
	{
		return Item.UniqueId == ItemId;
	});

	if (ItemIndex == INDEX_NONE)
	{
		return false;
	}

	// 移动到目标分类
	FFavoritesItem MovedItem = SourceCategory->Items[ItemIndex];
	MovedItem.SortOrder = TargetCategory->Items.Num();
	// 跨配置源移动时生成新的 UniqueId，确保在新配置中唯一
	if (SourceSource != TargetSource)
	{
		MovedItem.UniqueId = FGuid::NewGuid();
	}
	TargetCategory->Items.Add(MovedItem);
	SourceCategory->Items.RemoveAt(ItemIndex);

	MergeCategoriesData();
	SaveData();
	OnDataChanged.Broadcast();
	return true;
}

bool FAssetFavoritesDataManager::MoveItemToPosition(const FGuid& SourceCategoryId, const FGuid& ItemId, const FGuid& TargetCategoryId, int32 TargetPosition)
{
	EFavoritesConfigSource SourceSource = GetCategorySource(SourceCategoryId);
	EFavoritesConfigSource TargetSource = GetCategorySource(TargetCategoryId);

	FFavoritesData& SourceData = (SourceSource == EFavoritesConfigSource::Local) ? LocalData : SharedData;
	FFavoritesData& TargetData = (TargetSource == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	FFavoritesCategory* SourceCategory = nullptr;
	FFavoritesCategory* TargetCategory = nullptr;

	for (FFavoritesCategory& Cat : SourceData.Categories)
	{
		if (Cat.UniqueId == SourceCategoryId)
		{
			SourceCategory = &Cat;
			break;
		}
	}

	for (FFavoritesCategory& Cat : TargetData.Categories)
	{
		if (Cat.UniqueId == TargetCategoryId)
		{
			TargetCategory = &Cat;
			break;
		}
	}
	
	if (!SourceCategory || !TargetCategory)
	{
		return false;
	}

	int32 ItemIndex = SourceCategory->Items.IndexOfByPredicate([&ItemId](const FFavoritesItem& Item)
	{
		return Item.UniqueId == ItemId;
	});

	if (ItemIndex == INDEX_NONE)
	{
		return false;
	}

	// 从源分类移除
	FFavoritesItem MovedItem = SourceCategory->Items[ItemIndex];
	SourceCategory->Items.RemoveAt(ItemIndex);

	// 跨配置源移动时生成新的 UniqueId，确保在新配置中唯一
	if (SourceSource != TargetSource)
	{
		MovedItem.UniqueId = FGuid::NewGuid();
	}

	// 限制目标位置在有效范围内
	TargetPosition = FMath::Clamp(TargetPosition, 0, TargetCategory->Items.Num());

	// 插入到目标位置
	TargetCategory->Items.Insert(MovedItem, TargetPosition);

	// 更新目标分类的排序序号
	for (int32 i = 0; i < TargetCategory->Items.Num(); ++i)
	{
		TargetCategory->Items[i].SortOrder = i;
	}

	MergeCategoriesData();
	SaveData();
	OnDataChanged.Broadcast();
	return true;
}

bool FAssetFavoritesDataManager::IsItemFavorited(const FString& Path) const
{
	for (const FFavoritesCategory& Category : MergedCategories)
	{
		for (const FFavoritesItem& Item : Category.Items)
		{
			if (Item.Path == Path)
			{
				return true;
			}
		}
	}
	return false;
}

FFavoritesCategory* FAssetFavoritesDataManager::FindCategoryByItemPath(const FString& Path)
{
	for (FFavoritesCategory& Category : MergedCategories)
	{
		for (const FFavoritesItem& Item : Category.Items)
		{
			if (Item.Path == Path)
			{
				return &Category;
			}
		}
	}
	return nullptr;
}

FFavoritesCategory* FAssetFavoritesDataManager::FindCategoryById(const FGuid& CategoryId)
{
	for (FFavoritesCategory& Category : MergedCategories)
	{
		if (Category.UniqueId == CategoryId)
		{
			return &Category;
		}
	}
	return nullptr;
}

FFavoritesItem* FAssetFavoritesDataManager::FindItemById(const FGuid& CategoryId, const FGuid& ItemId)
{
	FFavoritesCategory* Category = FindCategoryById(CategoryId);
	if (!Category)
	{
		return nullptr;
	}

	for (FFavoritesItem& Item : Category->Items)
	{
		if (Item.UniqueId == ItemId)
		{
			return &Item;
		}
	}
	return nullptr;
}

namespace
{
	int32 CommonPrefixLength(const FString& A, const FString& B)
	{
		const int32 MaxLen = FMath::Min(A.Len(), B.Len());
		int32 Index = 0;
		while (Index < MaxLen && A[Index] == B[Index])
		{
			++Index;
		}
		return Index;
	}

	FAssetData PickUniqueClosest(const TArray<FAssetData>& Matches, const FString& OldPackageName)
	{
		if (Matches.Num() == 0)
		{
			return FAssetData();
		}
		if (Matches.Num() == 1)
		{
			return Matches[0];
		}

		int32 BestIndex = INDEX_NONE;
		int32 BestLen = -1;
		int32 SecondLen = -1;
		for (int32 i = 0; i < Matches.Num(); ++i)
		{
			const int32 Common = CommonPrefixLength(OldPackageName, Matches[i].PackageName.ToString());
			if (Common > BestLen)
			{
				SecondLen = BestLen;
				BestLen = Common;
				BestIndex = i;
			}
			else if (Common == BestLen)
			{
				SecondLen = Common;
			}
			else if (Common > SecondLen)
			{
				SecondLen = Common;
			}
		}

		// 同名资产有多个时，必须有一个路径明显更接近旧路径才自动认领
		if (BestIndex != INDEX_NONE && BestLen > SecondLen && BestLen >= 5)
		{
			return Matches[BestIndex];
		}
		return FAssetData();
	}

	void CollectNameMatches(IAssetRegistry& Registry, const FString& SearchPath, const FName TargetName, TArray<FAssetData>& OutMatches)
	{
		if (SearchPath.IsEmpty() || TargetName.IsNone())
		{
			return;
		}

		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(*SearchPath), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName == TargetName && !Asset.IsRedirector())
			{
				OutMatches.Add(Asset);
			}
		}
	}

	FAssetData FindMovedAssetByName(IAssetRegistry& Registry, const FSoftObjectPath& OldPath, bool bSearchWholeGame)
	{
		const FName TargetName(*OldPath.GetAssetName());
		if (TargetName.IsNone())
		{
			return FAssetData();
		}

		const FString OldPackageName = OldPath.GetLongPackageName();
		const FString ParentFolder = FPackageName::GetLongPackagePath(OldPackageName);

		TArray<FAssetData> Matches;
		if (!ParentFolder.IsEmpty())
		{
			CollectNameMatches(Registry, ParentFolder, TargetName, Matches);
			const FAssetData Picked = PickUniqueClosest(Matches, OldPackageName);
			if (Picked.IsValid())
			{
				return Picked;
			}
		}

		if (bSearchWholeGame && ParentFolder != TEXT("/Game"))
		{
			Matches.Reset();
			CollectNameMatches(Registry, TEXT("/Game"), TargetName, Matches);
			return PickUniqueClosest(Matches, OldPackageName);
		}

		return FAssetData();
	}
}

FAssetData FAssetFavoritesDataManager::ResolveAssetData(const FString& ObjectPath, bool bSearchMovedAssets)
{
	if (ObjectPath.IsEmpty())
	{
		return FAssetData();
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FSoftObjectPath SoftPath(ObjectPath);
	FAssetData AssetData = Registry.GetAssetByObjectPath(SoftPath);

	if (AssetData.IsValid() && AssetData.IsRedirector())
	{
		const FSoftObjectPath Redirected = Registry.GetRedirectedObjectPath(SoftPath);
		if (Redirected.IsValid())
		{
			FAssetData Destination = Registry.GetAssetByObjectPath(Redirected);
			if (Destination.IsValid() && !Destination.IsRedirector())
			{
				return Destination;
			}
		}
		AssetData = FAssetData();
	}

	if (AssetData.IsValid())
	{
		return AssetData;
	}

	if (!bSearchMovedAssets || Registry.IsLoadingAssets() || !SoftPath.IsValid())
	{
		return FAssetData();
	}

	// UI 查找只搜原目录树（通常就是策划把文件挪到子目录的情况）；全 /Game 扫描留给 RepairMovedItemPaths
	return FindMovedAssetByName(Registry, SoftPath, false);
}

bool FAssetFavoritesDataManager::ApplyResolvedPathToItem(FFavoritesItem& Item, const FAssetData& Resolved)
{
	if (!Resolved.IsValid())
	{
		return false;
	}

	const FString NewPath = Resolved.GetSoftObjectPath().ToString();
	if (NewPath.IsEmpty() || NewPath == Item.Path)
	{
		return false;
	}

	const FString OldAssetName = FSoftObjectPath(Item.Path).GetAssetName();
	UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Repair path '%s' -> '%s'"), *Item.Path, *NewPath);
	if (Item.DisplayName.IsEmpty() || Item.DisplayName == OldAssetName)
	{
		Item.DisplayName = Resolved.AssetName.ToString();
	}
	Item.Path = NewPath;
	return true;
}

void FAssetFavoritesDataManager::InvalidateGameNameIndexCache()
{
	CachedGameNameIndex.Empty();
	CachedGameNameIndexKeys.Empty();
	bGameNameIndexValid = false;
}

const TMap<FName, TArray<FAssetData>>& FAssetFavoritesDataManager::GetOrBuildGameNameIndex(
	IAssetRegistry& Registry, const TSet<FName>& NeededNames)
{
	// LoadData 与后续 FilesLoaded 共用同一次 /Game 扫描。
	// 无法自动认领的歧义同名项会一直留在 StillMissing，不能每次 FilesLoaded 都全量遍历。
	bool bKeysCovered = bGameNameIndexValid;
	if (bKeysCovered)
	{
		for (const FName& Name : NeededNames)
		{
			if (!CachedGameNameIndexKeys.Contains(Name))
			{
				bKeysCovered = false;
				break;
			}
		}
	}

	if (bKeysCovered)
	{
		return CachedGameNameIndex;
	}

	CachedGameNameIndex.Empty();
	TArray<FAssetData> AllGameAssets;
	Registry.GetAssetsByPath(FName(TEXT("/Game")), AllGameAssets, true);
	for (const FAssetData& Asset : AllGameAssets)
	{
		if (!Asset.IsRedirector() && NeededNames.Contains(Asset.AssetName))
		{
			CachedGameNameIndex.FindOrAdd(Asset.AssetName).Add(Asset);
		}
	}

	CachedGameNameIndexKeys = NeededNames;
	bGameNameIndexValid = true;
	return CachedGameNameIndex;
}

int32 FAssetFavoritesDataManager::RepairMovedItemPaths()
{
	if (!bIsLoaded)
	{
		return 0;
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	int32 RepairedCount = 0;
	TArray<FFavoritesItem*> StillMissing;

	auto RepairInData = [&](FFavoritesData& Data)
	{
		for (FFavoritesCategory& Category : Data.Categories)
		{
			for (FFavoritesItem& Item : Category.Items)
			{
				if (Item.ItemType != EFavoritesItemType::Asset)
				{
					continue;
				}

				const FAssetData Resolved = ResolveAssetData(Item.Path, true);
				if (Resolved.IsValid())
				{
					if (ApplyResolvedPathToItem(Item, Resolved))
					{
						++RepairedCount;
					}
				}
				else
				{
					StillMissing.Add(&Item);
				}
			}
		}
	};

	RepairInData(LocalData);
	RepairInData(SharedData);

	if (StillMissing.Num() > 0 && !Registry.IsLoadingAssets())
	{
		TSet<FName> NeededNames;
		for (const FFavoritesItem* Item : StillMissing)
		{
			const FName AssetName(*FSoftObjectPath(Item->Path).GetAssetName());
			if (!AssetName.IsNone())
			{
				NeededNames.Add(AssetName);
			}
		}

		if (NeededNames.Num() > 0)
		{
			const TMap<FName, TArray<FAssetData>>& NameIndex = GetOrBuildGameNameIndex(Registry, NeededNames);
			for (FFavoritesItem* Item : StillMissing)
			{
				const FSoftObjectPath OldPath(Item->Path);
				const FName AssetName(*OldPath.GetAssetName());
				if (const TArray<FAssetData>* Matches = NameIndex.Find(AssetName))
				{
					const FAssetData Picked = PickUniqueClosest(*Matches, OldPath.GetLongPackageName());
					if (ApplyResolvedPathToItem(*Item, Picked))
					{
						++RepairedCount;
					}
				}
			}
		}
	}

	if (RepairedCount > 0)
	{
		MergeCategoriesData();
		SaveData();
		OnDataChanged.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Repaired %d moved favorite path(s)"), RepairedCount);
	}

	return RepairedCount;
}

void FAssetFavoritesDataManager::BindAssetRegistryEvents()
{
	if (bRegistryEventsBound)
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = AssetRegistryModule.Get();
	AssetRenamedHandle = Registry.OnAssetRenamed().AddRaw(this, &FAssetFavoritesDataManager::HandleAssetRenamed);
	FilesLoadedHandle = Registry.OnFilesLoaded().AddRaw(this, &FAssetFavoritesDataManager::HandleFilesLoaded);
	bRegistryEventsBound = true;
}

void FAssetFavoritesDataManager::UnbindAssetRegistryEvents()
{
	if (!bRegistryEventsBound)
	{
		return;
	}

	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		IAssetRegistry& Registry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.OnAssetRenamed().Remove(AssetRenamedHandle);
		Registry.OnFilesLoaded().Remove(FilesLoadedHandle);
	}

	AssetRenamedHandle.Reset();
	FilesLoadedHandle.Reset();
	bRegistryEventsBound = false;
	InvalidateGameNameIndexCache();
}

void FAssetFavoritesDataManager::HandleFilesLoaded()
{
	RepairMovedItemPaths();
}

void FAssetFavoritesDataManager::HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
	if (!bIsLoaded || !AssetData.IsValid())
	{
		return;
	}

	const FString NewPath = AssetData.GetSoftObjectPath().ToString();
	const FString NormalizedOld = FSoftObjectPath(OldObjectPath).ToString();
	if (NewPath.IsEmpty() || (NewPath == OldObjectPath && NewPath == NormalizedOld))
	{
		return;
	}

	auto UpdateIn = [&](FFavoritesData& Data) -> bool
	{
		bool bChanged = false;
		for (FFavoritesCategory& Category : Data.Categories)
		{
			for (FFavoritesItem& Item : Category.Items)
			{
				if (Item.ItemType != EFavoritesItemType::Asset)
				{
					continue;
				}
				if (Item.Path == OldObjectPath || Item.Path == NormalizedOld)
				{
					const FString OldAssetName = FSoftObjectPath(Item.Path).GetAssetName();
					Item.Path = NewPath;
					if (Item.DisplayName.IsEmpty() || Item.DisplayName == OldAssetName)
					{
						Item.DisplayName = AssetData.AssetName.ToString();
					}
					bChanged = true;
				}
			}
		}
		return bChanged;
	};

	const bool bLocalChanged = UpdateIn(LocalData);
	const bool bSharedChanged = UpdateIn(SharedData);
	if (bLocalChanged || bSharedChanged)
	{
		MergeCategoriesData();
		SaveData();
		OnDataChanged.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Asset renamed/moved, favorite path '%s' -> '%s'"), *NormalizedOld, *NewPath);
	}
}

bool FAssetFavoritesDataManager::ValidateItem(const FFavoritesItem& Item) const
{
	if (Item.ItemType == EFavoritesItemType::Directory)
	{
		FString InternalPath = Item.Path;
		if (InternalPath.StartsWith(TEXT("/All/")))
		{
			InternalPath = InternalPath.Mid(4);
		}
		return FPaths::DirectoryExists(FPaths::ProjectContentDir() / InternalPath.Replace(TEXT("/Game/"), TEXT("")));
	}

	return ResolveAssetData(Item.Path, true).IsValid();
}

int32 FAssetFavoritesDataManager::RemoveInvalidItems()
{
	int32 RemovedCount = 0;

	// 处理本地数据
	for (FFavoritesCategory& Category : LocalData.Categories)
	{
		for (int32 i = Category.Items.Num() - 1; i >= 0; --i)
		{
			if (!ValidateItem(Category.Items[i]))
			{
				Category.Items.RemoveAt(i);
				RemovedCount++;
			}
		}
	}

	// 处理公共数据
	for (FFavoritesCategory& Category : SharedData.Categories)
	{
		for (int32 i = Category.Items.Num() - 1; i >= 0; --i)
		{
			if (!ValidateItem(Category.Items[i]))
			{
				Category.Items.RemoveAt(i);
				RemovedCount++;
			}
		}
	}

	if (RemovedCount > 0)
	{
		MergeCategoriesData();
		SaveData();
		OnDataChanged.Broadcast();
	}

	return RemovedCount;
}

EFavoritesItemStatus FAssetFavoritesDataManager::EvaluateItemStatus(const FFavoritesItem& Item) const
{
	if (Item.ItemType == EFavoritesItemType::Directory)
	{
		return ValidateItem(Item) ? EFavoritesItemStatus::Valid : EFavoritesItemStatus::Invalid;
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData Exact = Registry.GetAssetByObjectPath(FSoftObjectPath(Item.Path));
	if (Exact.IsValid() && !Exact.IsRedirector())
	{
		return EFavoritesItemStatus::Valid;
	}

	// 精确路径不可用：和界面行显示一致，按 Redirector / 资产名就近查找（只读，不改 Path）
	return ResolveAssetData(Item.Path, true).IsValid() ? EFavoritesItemStatus::Moved : EFavoritesItemStatus::Invalid;
}

FFavoritesImportResult FAssetFavoritesDataManager::ImportCategories(const TArray<FFavoritesCategory>& InCategories, const FGuid& AnchorCategoryId)
{
	FFavoritesImportResult Result;
	if (InCategories.Num() == 0)
	{
		return Result;
	}

	LoadData();

	// 两个来源的数组都按 SortOrder 稳定排序，保证数组下标顺序与显示顺序一致
	auto SortBySortOrder = [](FFavoritesData& Data)
	{
		Data.Categories.StableSort([](const FFavoritesCategory& A, const FFavoritesCategory& B)
		{
			return A.SortOrder < B.SortOrder;
		});
	};
	SortBySortOrder(LocalData);
	SortBySortOrder(SharedData);

	// 插入位置（按 EFavoritesConfigSource 取下标：Local=0, Shared=1）。
	// 共享区永远显示在个人区上方，两区各自排序：
	//  - 锚点与落点同来源：插到锚点之后
	//  - 锚点是共享、落点是个人：插到个人区最前面
	//  - 锚点是个人、落点是共享：追加到共享区末尾
	//  - 无锚点：追加到对应来源末尾
	int32 InsertIndex[2] = { LocalData.Categories.Num(), SharedData.Categories.Num() };
	if (AnchorCategoryId.IsValid() && FindCategoryById(AnchorCategoryId))
	{
		const EFavoritesConfigSource AnchorSource = GetCategorySource(AnchorCategoryId);
		const FFavoritesData& AnchorData = (AnchorSource == EFavoritesConfigSource::Local) ? LocalData : SharedData;
		const int32 AnchorIndex = AnchorData.Categories.IndexOfByPredicate([&AnchorCategoryId](const FFavoritesCategory& Cat)
		{
			return Cat.UniqueId == AnchorCategoryId;
		});
		if (AnchorIndex != INDEX_NONE)
		{
			InsertIndex[static_cast<int32>(AnchorSource)] = AnchorIndex + 1;
			if (AnchorSource == EFavoritesConfigSource::Shared)
			{
				InsertIndex[static_cast<int32>(EFavoritesConfigSource::Local)] = 0;
			}
		}
	}

	// 现有 GUID：保持输入 GUID 不变，仅冲突时才重新生成
	TSet<FGuid> UsedCategoryGuids;
	TSet<FGuid> UsedItemGuids;
	for (const FFavoritesData* Data : { &LocalData, &SharedData })
	{
		for (const FFavoritesCategory& Category : Data->Categories)
		{
			UsedCategoryGuids.Add(Category.UniqueId);
			for (const FFavoritesItem& Item : Category.Items)
			{
				UsedItemGuids.Add(Item.UniqueId);
			}
		}
	}

	auto ClaimGuid = [&Result](TSet<FGuid>& Used, FGuid& Guid)
	{
		if (!Guid.IsValid())
		{
			Guid = FGuid::NewGuid();
		}
		else if (Used.Contains(Guid))
		{
			Guid = FGuid::NewGuid();
			++Result.RegeneratedGuids;
		}
		Used.Add(Guid);
	};

	bool bInsertedCategory[2] = { false, false };
	TArray<FFavoritesItem> AddedItemList;

	for (const FFavoritesCategory& In : InCategories)
	{
		const int32 SourceIndex = static_cast<int32>(In.ConfigSource);
		FFavoritesData& Data = (In.ConfigSource == EFavoritesConfigSource::Local) ? LocalData : SharedData;

		// 同来源同名分类：合并
		FFavoritesCategory* Target = Data.Categories.FindByPredicate([&In](const FFavoritesCategory& Cat)
		{
			return Cat.Name == In.Name;
		});

		if (Target)
		{
			++Result.MergedCategories;
		}
		else
		{
			FFavoritesCategory NewCategory(In.Name, In.ConfigSource);
			NewCategory.Color = In.Color;
			NewCategory.UniqueId = In.UniqueId;
			ClaimGuid(UsedCategoryGuids, NewCategory.UniqueId);

			const int32 Index = FMath::Clamp(InsertIndex[SourceIndex], 0, Data.Categories.Num());
			Data.Categories.Insert(NewCategory, Index);
			InsertIndex[SourceIndex] = Index + 1;
			bInsertedCategory[SourceIndex] = true;
			Target = &Data.Categories[Index];
			++Result.NewCategories;
		}

		// 条目按 Path 去重追加；已有条目的备注和显示名保持不变
		for (const FFavoritesItem& InItem : In.Items)
		{
			if (InItem.Path.IsEmpty())
			{
				continue;
			}

			const bool bDuplicate = Target->Items.ContainsByPredicate([&InItem](const FFavoritesItem& Existing)
			{
				return Existing.Path == InItem.Path;
			});
			if (bDuplicate)
			{
				++Result.SkippedDuplicateItems;
				continue;
			}

			FFavoritesItem NewItem = InItem;
			ClaimGuid(UsedItemGuids, NewItem.UniqueId);
			NewItem.SortOrder = Target->Items.Num();
			Target->Items.Add(NewItem);
			AddedItemList.Add(NewItem);
			++Result.AddedItems;
		}
	}

	if (Result.NewCategories == 0 && Result.AddedItems == 0)
	{
		return Result;
	}

	// 有新建分类的来源重新编号 SortOrder
	for (int32 SourceIndex = 0; SourceIndex < 2; ++SourceIndex)
	{
		if (!bInsertedCategory[SourceIndex])
		{
			continue;
		}
		FFavoritesData& Data = (SourceIndex == static_cast<int32>(EFavoritesConfigSource::Local)) ? LocalData : SharedData;
		for (int32 i = 0; i < Data.Categories.Num(); ++i)
		{
			Data.Categories[i].SortOrder = i;
		}
	}

	// 只报告新增条目里当前工程找不到的路径，不做任何路径修复
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Result.bRegistryLoading = Registry.IsLoadingAssets();
	if (!Result.bRegistryLoading)
	{
		for (const FFavoritesItem& Item : AddedItemList)
		{
			if (!ValidateItem(Item))
			{
				Result.MissingPaths.Add(Item.Path);
			}
		}
	}

	MergeCategoriesData();
	SaveData();
	OnDataChanged.Broadcast();
	return Result;
}

void FAssetFavoritesDataManager::ReloadFileIfChanged(EFavoritesConfigSource Source, bool& bOutReloaded, bool& bOutMissing, bool& bOutParseFailed)
{
	const bool bLocal = (Source == EFavoritesConfigSource::Local);
	const FString FilePath = bLocal ? GetLocalDataFilePath() : GetSharedDataFilePath();
	FFileStamp& Recorded = bLocal ? LocalFileStamp : SharedFileStamp;

	const FFileStamp Current = QueryFileStamp(FilePath);
	if (Current == Recorded)
	{
		return;
	}

	if (!Current.Exists())
	{
		// 文件被删除或被 revert：保留内存数据，下次保存时会重新写出
		bOutMissing = true;
		return;
	}

	FFavoritesData Parsed;
	switch (ReadDataFile(FilePath, Source, Parsed))
	{
	case EFavoritesReadResult::Ok:
		(bLocal ? LocalData : SharedData) = MoveTemp(Parsed);
		Recorded = Current;
		bOutReloaded = true;
		break;
	case EFavoritesReadResult::Missing:
		bOutMissing = true;
		break;
	case EFavoritesReadResult::ParseFailed:
		// 文件损坏：保留内存数据，不清空，避免下一次保存把空数据写回去
		bOutParseFailed = true;
		break;
	}
}

FFavoritesReloadReport FAssetFavoritesDataManager::ReloadAll()
{
	FFavoritesReloadReport Report;

	if (!bIsLoaded)
	{
		LoadData();
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Report.bStatusSkipped = Registry.IsLoadingAssets();

	auto MakeKey = [](const FFavoritesCategory& Category, const FFavoritesItem& Item)
	{
		return Category.UniqueId.ToString() + TEXT("|") + Item.UniqueId.ToString();
	};

	// 1. Reload 前的状态快照（即界面当前看到的状态）
	TMap<FString, EFavoritesItemStatus> Before;
	if (!Report.bStatusSkipped)
	{
		for (const FFavoritesCategory& Category : MergedCategories)
		{
			for (const FFavoritesItem& Item : Category.Items)
			{
				Before.Add(MakeKey(Category, Item), EvaluateItemStatus(Item));
			}
		}
	}

	// 2. 检查两个 JSON 是否被外部更新
	ReloadFileIfChanged(EFavoritesConfigSource::Local, Report.bLocalReloaded, Report.bLocalMissing, Report.bLocalParseFailed);
	ReloadFileIfChanged(EFavoritesConfigSource::Shared, Report.bSharedReloaded, Report.bSharedMissing, Report.bSharedParseFailed);
	if (Report.bLocalReloaded || Report.bSharedReloaded)
	{
		MergeCategoriesData();
	}

	if (!Report.bStatusSkipped)
	{
		// 3. 注册表里没有、磁盘上存在的资产：强制重新扫描（应对 P4 sync 后目录监视滞后）
		const FString Extensions[2] = { FPackageName::GetAssetPackageExtension(), FPackageName::GetMapPackageExtension() };
		TArray<FString> ScanFiles;
		TSet<FString> ScanFileSet;
		for (const FFavoritesCategory& Category : MergedCategories)
		{
			for (const FFavoritesItem& Item : Category.Items)
			{
				if (Item.ItemType != EFavoritesItemType::Asset)
				{
					continue;
				}

				const FSoftObjectPath SoftPath(Item.Path);
				if (!SoftPath.IsValid() || Registry.GetAssetByObjectPath(SoftPath).IsValid())
				{
					continue;
				}

				const FString PackageName = SoftPath.GetLongPackageName();
				if (PackageName.IsEmpty())
				{
					continue;
				}

				for (const FString& Extension : Extensions)
				{
					FString Filename;
					if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, Extension) && FPaths::FileExists(Filename))
					{
						const FString FullPath = FPaths::ConvertRelativePathToFull(Filename);
						if (!ScanFileSet.Contains(FullPath))
						{
							ScanFileSet.Add(FullPath);
							ScanFiles.Add(FullPath);
						}
						break;
					}
				}
			}
		}

		if (ScanFiles.Num() > 0)
		{
			Registry.ScanModifiedAssetFiles(ScanFiles);
			Report.ScannedFiles = ScanFiles.Num();
		}

		// 4. 重新评估全部条目，并与 Reload 前对比
		for (const FFavoritesCategory& Category : MergedCategories)
		{
			for (const FFavoritesItem& Item : Category.Items)
			{
				const EFavoritesItemStatus Status = EvaluateItemStatus(Item);
				switch (Status)
				{
				case EFavoritesItemStatus::Valid:   ++Report.ValidCount;   break;
				case EFavoritesItemStatus::Moved:   ++Report.MovedCount;   break;
				case EFavoritesItemStatus::Invalid: ++Report.InvalidCount; break;
				}

				if (const EFavoritesItemStatus* Previous = Before.Find(MakeKey(Category, Item)))
				{
					if (*Previous != EFavoritesItemStatus::Valid && Status == EFavoritesItemStatus::Valid)
					{
						++Report.BecameValid;
						UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Reload - item became valid: %s"), *Item.Path);
					}
					else if (*Previous != EFavoritesItemStatus::Invalid && Status == EFavoritesItemStatus::Invalid)
					{
						++Report.BecameInvalid;
						UE_LOG(LogTemp, Warning, TEXT("AssetFavorites: Reload - item became invalid: %s"), *Item.Path);
					}
				}
			}
		}
	}

	// 5. 广播刷新：界面重建行，重新查询每个条目的类型/有效状态。
	// 即使两个 JSON 都没变也要刷新，因为状态变化来自资产而不是配置文件。
	OnDataChanged.Broadcast();
	return Report;
}

void FAssetFavoritesDataManager::SortCategories()
{
	LocalData.Categories.Sort([](const FFavoritesCategory& A, const FFavoritesCategory& B)
	{
		return A.SortOrder < B.SortOrder;
	});
	SharedData.Categories.Sort([](const FFavoritesCategory& A, const FFavoritesCategory& B)
	{
		return A.SortOrder < B.SortOrder;
	});
	MergeCategoriesData();
}

void FAssetFavoritesDataManager::SortItemsInCategory(const FGuid& CategoryId)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	for (FFavoritesCategory& Category : TargetData.Categories)
	{
		if (Category.UniqueId == CategoryId)
		{
			Category.Items.Sort([](const FFavoritesItem& A, const FFavoritesItem& B)
			{
				return A.SortOrder < B.SortOrder;
			});
			MergeCategoriesData();
			break;
		}
	}
}

bool FAssetFavoritesDataManager::MoveCategoryPosition(const FGuid& CategoryId, int32 NewPosition)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	int32 OldIndex = TargetData.Categories.IndexOfByPredicate([&CategoryId](const FFavoritesCategory& Cat)
	{
		return Cat.UniqueId == CategoryId;
	});

	if (OldIndex == INDEX_NONE || NewPosition < 0 || NewPosition >= TargetData.Categories.Num())
	{
		return false;
	}

	FFavoritesCategory MovedCategory = TargetData.Categories[OldIndex];
	TargetData.Categories.RemoveAt(OldIndex);
	TargetData.Categories.Insert(MovedCategory, NewPosition);

	// 更新排序序号
	for (int32 i = 0; i < TargetData.Categories.Num(); ++i)
	{
		TargetData.Categories[i].SortOrder = i;
	}

	MergeCategoriesData();
	SaveData();
	OnDataChanged.Broadcast();
	return true;
}

bool FAssetFavoritesDataManager::MoveItemPosition(const FGuid& CategoryId, const FGuid& ItemId, int32 NewPosition)
{
	EFavoritesConfigSource Source = GetCategorySource(CategoryId);
	FFavoritesData& TargetData = (Source == EFavoritesConfigSource::Local) ? LocalData : SharedData;

	FFavoritesCategory* Category = nullptr;
	for (FFavoritesCategory& Cat : TargetData.Categories)
	{
		if (Cat.UniqueId == CategoryId)
		{
			Category = &Cat;
			break;
		}
	}

	if (!Category)
	{
		return false;
	}

	int32 OldIndex = Category->Items.IndexOfByPredicate([&ItemId](const FFavoritesItem& Item)
	{
		return Item.UniqueId == ItemId;
	});

	if (OldIndex == INDEX_NONE || NewPosition < 0)
	{
		return false;
	}

	// 限制 NewPosition 在有效范围内
	NewPosition = FMath::Min(NewPosition, Category->Items.Num() - 1);

	// 如果位置没变，不需要操作
	if (OldIndex == NewPosition)
	{
		return true;
	}

	FFavoritesItem MovedItem = Category->Items[OldIndex];
	Category->Items.RemoveAt(OldIndex);
	
	// 移除后需要调整目标位置
	if (NewPosition > OldIndex)
	{
		NewPosition--;
	}
	NewPosition = FMath::Clamp(NewPosition, 0, Category->Items.Num());
	
	Category->Items.Insert(MovedItem, NewPosition);

	// 更新排序序号
	for (int32 i = 0; i < Category->Items.Num(); ++i)
	{
		Category->Items[i].SortOrder = i;
	}

	MergeCategoriesData();
	SaveData();
	OnDataChanged.Broadcast();
	return true;
}
