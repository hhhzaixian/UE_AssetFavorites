// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetFavoritesTypes.h"
#include "AssetRegistry/AssetData.h"

class IAssetRegistry;
class FJsonObject;

DECLARE_MULTICAST_DELEGATE(FOnFavoritesDataChanged);

/** 收藏项状态（Reload 检查用） */
enum class EFavoritesItemStatus : uint8
{
	Valid,    // 精确路径可用
	Moved,    // 精确路径不存在（或是 Redirector），但按名称能就近找到
	Invalid   // 找不到
};

/** 导入分类（粘贴）的结果 */
struct FFavoritesImportResult
{
	int32 NewCategories = 0;
	int32 MergedCategories = 0;
	int32 AddedItems = 0;
	int32 SkippedDuplicateItems = 0;
	/** 与现有 GUID 冲突而被重新生成的数量（未冲突时保持原 GUID） */
	int32 RegeneratedGuids = 0;
	/** 资产注册表仍在加载，未检查路径有效性 */
	bool bRegistryLoading = false;
	/** 新增条目中当前工程找不到的路径 */
	TArray<FString> MissingPaths;
};

/** 批量添加收藏项的结果 */
struct FFavoritesAddItemsResult
{
	int32 Added = 0;
	int32 SkippedDuplicate = 0;
};

/** Reload（重新加载收藏夹）的报告 */
struct FFavoritesReloadReport
{
	bool bLocalReloaded = false;
	bool bSharedReloaded = false;
	/** 文件已不存在，保留内存数据 */
	bool bLocalMissing = false;
	bool bSharedMissing = false;
	/** 文件解析失败，保留内存数据 */
	bool bLocalParseFailed = false;
	bool bSharedParseFailed = false;

	/** 资产注册表仍在加载，已跳过状态检查 */
	bool bStatusSkipped = false;
	/** 重新扫描的磁盘文件数（注册表里没有、磁盘上存在） */
	int32 ScannedFiles = 0;
	int32 ValidCount = 0;
	int32 MovedCount = 0;
	int32 InvalidCount = 0;
	/** 相对 Reload 前，变为有效 / 变为无效的条目数 */
	int32 BecameValid = 0;
	int32 BecameInvalid = 0;
};

/**
 * 收藏数据管理器
 * 负责数据的加载、保存、CRUD 操作
 * 支持本地配置（不提交git）和公共配置（提交git）
 */
class FAssetFavoritesDataManager
{
public:
	/** 获取单例实例 */
	static FAssetFavoritesDataManager& Get();

	/** 加载数据 */
	void LoadData();

	/** 保存数据（保存到对应的配置源） */
	void SaveData();

	/** 获取所有分类（合并后的视图） */
	TArray<FFavoritesCategory>& GetCategories() { return MergedCategories; }
	const TArray<FFavoritesCategory>& GetCategories() const { return MergedCategories; }

	/** 添加分类 */
	FFavoritesCategory& AddCategory(const FString& Name, EFavoritesConfigSource Source = EFavoritesConfigSource::Local);

	/** 移除分类 */
	bool RemoveCategory(const FGuid& CategoryId);

	/** 重命名分类 */
	bool RenameCategory(const FGuid& CategoryId, const FString& NewName);

	/** 设置分类颜色 */
	bool SetCategoryColor(const FGuid& CategoryId, const FLinearColor& Color);

	/** 设置分类展开状态 */
	bool SetCategoryExpanded(const FGuid& CategoryId, bool bExpanded);

	/** 添加收藏项到分类 */
	FFavoritesItem* AddItem(const FGuid& CategoryId, const FString& Path, EFavoritesItemType ItemType);

	/**
	 * 批量添加收藏项。已存在的 Path 跳过。整批只保存一次、广播一次。
	 * Pair：Path + ItemType。
	 */
	FFavoritesAddItemsResult AddItems(const FGuid& CategoryId, const TArray<TPair<FString, EFavoritesItemType>>& InItems);

	/** 移除收藏项 */
	bool RemoveItem(const FGuid& CategoryId, const FGuid& ItemId);

	/** 批量移除选中的收藏项 */
	void RemoveSelectedItems(const TArray<TSharedPtr<struct FFavoritesTreeNode>>& SelectedNodes);

	/** 设置收藏项备注 */
	bool SetItemNote(const FGuid& CategoryId, const FGuid& ItemId, const FString& Note);

	/** 设置收藏项显示名称 */
	bool SetItemDisplayName(const FGuid& CategoryId, const FGuid& ItemId, const FString& DisplayName);

	/** 移动收藏项到另一个分类 */
	bool MoveItem(const FGuid& SourceCategoryId, const FGuid& ItemId, const FGuid& TargetCategoryId);

	/** 移动收藏项到另一个分类的指定位置（原子操作） */
	bool MoveItemToPosition(const FGuid& SourceCategoryId, const FGuid& ItemId, const FGuid& TargetCategoryId, int32 TargetPosition);

	/** 检查资产是否已收藏 */
	bool IsItemFavorited(const FString& Path) const;

	/** 查找收藏项所在的分类 */
	FFavoritesCategory* FindCategoryByItemPath(const FString& Path);

	/** 根据ID查找分类 */
	FFavoritesCategory* FindCategoryById(const FGuid& CategoryId);

	/** 根据ID查找收藏项 */
	FFavoritesItem* FindItemById(const FGuid& CategoryId, const FGuid& ItemId);

	/** 验证资产是否有效（是否存在）。会跟随 Redirector，并在路径失效时按资产名就近查找。 */
	bool ValidateItem(const FFavoritesItem& Item) const;

	/**
	 * 解析收藏项对应的资产。
	 * 顺序：精确 ObjectPath → 跟随 Redirector →（可选）按资产名在原目录树 / 全 /Game 中就近匹配。
	 * UE 资产没有跨路径的稳定 GUID，收藏项 UniqueId 只标识收藏记录本身。
	 */
	static FAssetData ResolveAssetData(const FString& ObjectPath, bool bSearchMovedAssets = true);

	/**
	 * 扫描收藏列表，把已搬家/已重定向的资产 Path 写回 JSON。
	 * @return 实际改写的条目数
	 */
	int32 RepairMovedItemPaths();

	/** 注销 Asset Registry 回调（模块 Shutdown 时调用，避免热更/卸载后悬空） */
	void UnbindAssetRegistryEvents();

	/** 移除所有无效的收藏项 */
	int32 RemoveInvalidItems();

	/** 对分类排序 */
	void SortCategories();

	/** 对分类内的收藏项排序 */
	void SortItemsInCategory(const FGuid& CategoryId);

	/** 移动分类位置 */
	bool MoveCategoryPosition(const FGuid& CategoryId, int32 NewPosition);

	/** 移动收藏项位置 */
	bool MoveItemPosition(const FGuid& CategoryId, const FGuid& ItemId, int32 NewPosition);

	/**
	 * 导入分类（粘贴）。
	 * InCategories 的 ConfigSource 即最终落点来源。
	 * - 同来源同名分类：合并，条目按 Path 去重，已有条目的备注和显示名保持不变
	 * - 否则新建分类；新建分类和新增条目保持输入的 UniqueId，仅与现有 GUID 冲突时才重新生成
	 * - AnchorCategoryId 有效时插到它之后（跨来源的规则见实现），无效时追加到对应来源末尾
	 * 整批只保存一次、广播一次。不做路径修复。
	 */
	FFavoritesImportResult ImportCategories(const TArray<FFavoritesCategory>& InCategories, const FGuid& AnchorCategoryId);

	/**
	 * 重新加载：检查两个 JSON 是否被外部更新并重读，再重新扫描磁盘上已存在但注册表里缺失的资产，
	 * 最后刷新全部条目的有效状态并广播刷新。JSON 缺失或解析失败时保留内存数据。
	 */
	FFavoritesReloadReport ReloadAll();

	/** 评估单个收藏项的当前状态（不修改任何数据） */
	EFavoritesItemStatus EvaluateItemStatus(const FFavoritesItem& Item) const;

	/** 分类 -> JSON。bIncludeExpanded 为 false 时不写 bExpanded（共享配置和剪贴板都不写） */
	static TSharedRef<FJsonObject> CategoryToJson(const FFavoritesCategory& Category, bool bIncludeExpanded);

	/** JSON -> 分类（含条目）。不处理 bExpanded 与 ConfigSource；UniqueId 缺失或非法时保留 OutCategory 原有值 */
	static bool CategoryFromJson(const TSharedPtr<FJsonObject>& CategoryObject, FFavoritesCategory& OutCategory);

	/** 获取本地数据文件路径 */
	FString GetLocalDataFilePath() const;

	/** 获取公共数据文件路径 */
	FString GetSharedDataFilePath() const;

	/** 获取数据文件路径（返回本地路径，兼容旧接口） */
	FString GetDataFilePath() const { return GetLocalDataFilePath(); }

	/** 数据变更委托 */
	FOnFavoritesDataChanged OnDataChanged;

	/** 设置待编辑的 Item ID（添加备注后自动进入编辑模式） */
	void SetPendingEditItemId(const FGuid& ItemId) { PendingEditItemId = ItemId; }

	/** 获取并清除待编辑的 Item ID */
	FGuid ConsumePendingEditItemId() 
	{ 
		FGuid Result = PendingEditItemId; 
		PendingEditItemId.Invalidate(); 
		return Result; 
	}

private:
	FAssetFavoritesDataManager();
	~FAssetFavoritesDataManager();

	/** 配置文件读取结果 */
	enum class EFavoritesReadResult : uint8
	{
		Ok,
		Missing,      // 文件不存在
		ParseFailed   // 文件存在但不是合法 JSON
	};

	/** 文件状态（时间戳 + 大小），用于判断 JSON 是否被外部更新 */
	struct FFileStamp
	{
		FDateTime Time = FDateTime::MinValue();
		int64 Size = -1;

		bool Exists() const { return Size >= 0; }
		bool operator==(const FFileStamp& Other) const { return Time == Other.Time && Size == Other.Size; }
		bool operator!=(const FFileStamp& Other) const { return !(*this == Other); }
	};

	static FFileStamp QueryFileStamp(const FString& FilePath);

	/** 读取并解析单个配置文件到 OutData；仅在返回 Ok 时才写入 OutData */
	EFavoritesReadResult ReadDataFile(const FString& FilePath, EFavoritesConfigSource Source, FFavoritesData& OutData);

	/** 若文件相对上次记录有变化则重读并替换对应数据 */
	void ReloadFileIfChanged(EFavoritesConfigSource Source, bool& bOutReloaded, bool& bOutMissing, bool& bOutParseFailed);

	/** 记录某个配置文件当前的时间戳（自己写盘后调用，避免把自己的写入当成外部修改） */
	void RecordFileStamp(EFavoritesConfigSource Source);

	/** 加载单个配置文件（读取失败时 OutData 为空） */
	void LoadDataFromFile(const FString& FilePath, EFavoritesConfigSource Source, FFavoritesData& OutData);

	/** 保存单个配置文件 */
	void SaveDataToFile(const FString& FilePath, const FFavoritesData& InData, EFavoritesConfigSource Source);

	/** 合并本地和公共数据 */
	void MergeCategoriesData();

	/** 根据分类ID找到其配置来源 */
	EFavoritesConfigSource GetCategorySource(const FGuid& CategoryId) const;

	void BindAssetRegistryEvents();
	void HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void HandleFilesLoaded();

	/** 把 Item.Path 改成 Resolved 对应的新路径；DisplayName 若仍是旧资产名则一并更新 */
	static bool ApplyResolvedPathToItem(FFavoritesItem& Item, const FAssetData& Resolved);

	/** 获取或重建全 /Game 的 AssetName 索引（仅收录 NeededNames，避免 FilesLoaded 重复全量扫描） */
	const TMap<FName, TArray<FAssetData>>& GetOrBuildGameNameIndex(IAssetRegistry& Registry, const TSet<FName>& NeededNames);

	void InvalidateGameNameIndexCache();

	/** 本地数据 */
	FFavoritesData LocalData;

	/** 公共数据 */
	FFavoritesData SharedData;

	/** 合并后的分类列表（用于UI显示） */
	TArray<FFavoritesCategory> MergedCategories;

	/** 是否已加载 */
	bool bIsLoaded = false;

	/** 待编辑的 Item ID（用于添加备注后自动进入编辑模式） */
	FGuid PendingEditItemId;

	/** 上次读写后记录的配置文件状态 */
	FFileStamp LocalFileStamp;
	FFileStamp SharedFileStamp;

	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle FilesLoadedHandle;
	bool bRegistryEventsBound = false;

	/** 全 /Game NameIndex 缓存：覆盖上次扫描的 NeededNames，避免 FilesLoaded 重复全量遍历 */
	TMap<FName, TArray<FAssetData>> CachedGameNameIndex;
	TSet<FName> CachedGameNameIndexKeys;
	bool bGameNameIndexValid = false;
};
