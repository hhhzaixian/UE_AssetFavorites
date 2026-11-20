// Copyright LoF Team. All Rights Reserved.

#include "AssetFavoritesTransfer.h"
#include "AssetFavoritesDataManager.h"
#include "AssetFavoritesSettings.h"
#include "AssetFavoritesSourceControlUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "AssetFavoritesTransfer"

namespace
{
	/** 剪贴板载荷格式标识与版本。后续新增字段时保持向后兼容，仅在不兼容变更时提升版本。 */
	const TCHAR* const PayloadFormat = TEXT("AssetFavorites.Categories");
	constexpr int32 PayloadVersion = 1;

	/** 粘贴通知里最多列出多少个找不到的路径，其余写入 Output Log */
	constexpr int32 MaxMissingPathsInNotification = 5;

	void ShowNotification(const FText& Message, bool bWarning, float ExpireDuration = 6.0f)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = ExpireDuration;
		Info.bUseThrobber = false;
		Info.bFireAndForget = true;
		Info.Image = FAppStyle::Get().GetBrush(bWarning ? FName("Icons.WarningWithColor") : FName("Icons.SuccessWithColor"));
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	/** 轻量判断：以 { 开头且包含格式标识 */
	bool LooksLikePayload(const FString& Text)
	{
		return Text.Len() > 2 && Text.TrimStart().StartsWith(TEXT("{")) && Text.Contains(PayloadFormat);
	}

	/** 解析剪贴板载荷。成功时每个分类的 ConfigSource 为载荷里标记的来源。 */
	bool ParsePayload(const FString& Text, TArray<FFavoritesCategory>& OutCategories, FText& OutError)
	{
		if (!LooksLikePayload(Text))
		{
			OutError = LOCTEXT("NoPayload", "剪贴板中没有收藏夹分类数据。");
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = LOCTEXT("PayloadBadJson", "剪贴板中的收藏夹数据格式错误（不是合法的 JSON）。");
			return false;
		}

		FString Format;
		if (!Root->TryGetStringField(TEXT("Format"), Format) || Format != PayloadFormat)
		{
			OutError = LOCTEXT("NoPayload2", "剪贴板中没有收藏夹分类数据。");
			return false;
		}

		int32 Version = 0;
		if (!Root->TryGetNumberField(TEXT("FormatVersion"), Version) || Version < 1)
		{
			OutError = LOCTEXT("PayloadNoVersion", "收藏夹数据缺少有效的版本号。");
			return false;
		}
		if (Version > PayloadVersion)
		{
			OutError = FText::Format(LOCTEXT("PayloadNewerVersion", "收藏夹数据版本 v{0} 高于当前支持的 v{1}，请更新插件后再粘贴。"), Version, PayloadVersion);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* CategoriesArray = nullptr;
		if (!Root->TryGetArrayField(TEXT("Categories"), CategoriesArray))
		{
			OutError = LOCTEXT("PayloadNoCategories", "收藏夹数据中没有分类。");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& CategoryValue : *CategoriesArray)
		{
			const TSharedPtr<FJsonObject>* CategoryObject = nullptr;
			if (!CategoryValue.IsValid() || !CategoryValue->TryGetObject(CategoryObject) || !CategoryObject)
			{
				continue;
			}

			FFavoritesCategory Category;
			if (!FAssetFavoritesDataManager::CategoryFromJson(*CategoryObject, Category))
			{
				continue;
			}

			FString SourceString;
			(*CategoryObject)->TryGetStringField(TEXT("Source"), SourceString);
			Category.ConfigSource = SourceString.Equals(TEXT("Shared"), ESearchCase::IgnoreCase)
				? EFavoritesConfigSource::Shared
				: EFavoritesConfigSource::Local;

			if (Category.Name.IsEmpty())
			{
				Category.Name = LOCTEXT("UnnamedCategory", "未命名分类").ToString();
			}

			// 丢弃没有路径或类型非法的条目
			Category.Items.RemoveAll([](const FFavoritesItem& Item)
			{
				return Item.Path.IsEmpty()
					|| static_cast<uint8>(Item.ItemType) > static_cast<uint8>(EFavoritesItemType::Directory);
			});

			OutCategories.Add(MoveTemp(Category));
		}

		if (OutCategories.Num() == 0)
		{
			OutError = LOCTEXT("PayloadEmpty", "收藏夹数据中没有可粘贴的分类。");
			return false;
		}

		return true;
	}

}

void FAssetFavoritesTransfer::CopyCategories(const TArray<FGuid>& CategoryIds)
{
	FAssetFavoritesDataManager& DataManager = FAssetFavoritesDataManager::Get();
	DataManager.LoadData();

	const TSet<FGuid> IdSet(CategoryIds);

	TArray<TSharedPtr<FJsonValue>> CategoriesArray;
	int32 LocalCount = 0;
	int32 SharedCount = 0;
	int32 ItemCount = 0;

	// 按显示顺序（共享区在前、个人区在后）输出，而不是按选中顺序
	for (const FFavoritesCategory& Category : DataManager.GetCategories())
	{
		if (!IdSet.Contains(Category.UniqueId))
		{
			continue;
		}

		// 载荷里不写展开状态；来源是运行时字段，需要单独写入
		TSharedRef<FJsonObject> CategoryObject = FAssetFavoritesDataManager::CategoryToJson(Category, /*bIncludeExpanded=*/false);
		const bool bShared = (Category.ConfigSource == EFavoritesConfigSource::Shared);
		CategoryObject->SetStringField(TEXT("Source"), bShared ? TEXT("Shared") : TEXT("Local"));
		CategoriesArray.Add(MakeShared<FJsonValueObject>(CategoryObject));

		(bShared ? SharedCount : LocalCount) += 1;
		ItemCount += Category.Items.Num();
	}

	if (CategoriesArray.Num() == 0)
	{
		return;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("Format"), PayloadFormat);
	Root->SetNumberField(TEXT("FormatVersion"), PayloadVersion);
	Root->SetArrayField(TEXT("Categories"), CategoriesArray);

	FString Text;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);

	FPlatformApplicationMisc::ClipboardCopy(*Text);

	ShowNotification(FText::Format(
		LOCTEXT("CopyDone", "已复制 {0} 个分类（个人 {1} / 共享 {2}），共 {3} 项"),
		CategoriesArray.Num(), LocalCount, SharedCount, ItemCount), false, 3.0f);
}

bool FAssetFavoritesTransfer::ClipboardHasFavorites()
{
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	return LooksLikePayload(Text);
}

void FAssetFavoritesTransfer::PasteFromClipboard(const FGuid& AnchorCategoryId)
{
	FAssetFavoritesDataManager& DataManager = FAssetFavoritesDataManager::Get();
	DataManager.LoadData();

	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);

	TArray<FFavoritesCategory> Categories;
	FText Error;
	if (!ParsePayload(Text, Categories, Error))
	{
		ShowNotification(Error, true);
		return;
	}

	int32 SharedCount = 0;
	for (const FFavoritesCategory& Category : Categories)
	{
		if (Category.ConfigSource == EFavoritesConfigSource::Shared)
		{
			++SharedCount;
		}
	}

	// 含共享组时只弹一次确认：保持共享 / 全部转个人 / 取消
	bool bKeepShared = (SharedCount > 0);
	if (SharedCount > 0)
	{
		const EAppReturnType::Type Choice = FMessageDialog::Open(
			EAppMsgType::YesNoCancel,
			FText::Format(LOCTEXT("PasteSharedConfirm",
				"粘贴内容包含 {0} 个共享组。\n共享组会写入公共配置（Config/AssetFavorites_Shared.json），团队可见，需要 P4 checkout。\n\n是：保持共享\n否：全部转为个人\n取消：取消粘贴"),
				SharedCount),
			LOCTEXT("PasteTitle", "粘贴收藏夹分类"));

		if (Choice == EAppReturnType::Cancel)
		{
			return;
		}
		bKeepShared = (Choice == EAppReturnType::Yes);
	}

	// 保持共享时，公共配置必须可写；否则让用户选择转成个人或取消，避免数据只存在内存里、重启后丢失
	if (bKeepShared)
	{
		const FString SharedPath = DataManager.GetSharedDataFilePath();
		if (!FAssetFavoritesSourceControlUtils::EnsureFileWritable(SharedPath))
		{
			const EAppReturnType::Type Choice = FMessageDialog::Open(
				EAppMsgType::YesNo,
				FText::Format(LOCTEXT("PasteSharedNotWritable",
					"公共配置文件无法写入（只读且 checkout 失败）：\n{0}\n\n是：转成个人\n否：取消粘贴"),
					FText::FromString(SharedPath)),
				LOCTEXT("PasteTitle2", "粘贴收藏夹分类"));

			if (Choice != EAppReturnType::Yes)
			{
				return;
			}
			bKeepShared = false;
		}
	}

	for (FFavoritesCategory& Category : Categories)
	{
		Category.ConfigSource = (Category.ConfigSource == EFavoritesConfigSource::Shared && bKeepShared)
			? EFavoritesConfigSource::Shared
			: EFavoritesConfigSource::Local;
	}

	const FFavoritesImportResult Result = DataManager.ImportCategories(Categories, AnchorCategoryId);

	// 汇总报告
	FString Message = FText::Format(
		LOCTEXT("PasteSummary", "粘贴完成：新建 {0} 个分类，合并 {1} 个分类；新增 {2} 项，重复跳过 {3} 项。"),
		Result.NewCategories, Result.MergedCategories, Result.AddedItems, Result.SkippedDuplicateItems).ToString();

	if (Result.RegeneratedGuids > 0)
	{
		Message += TEXT("\n") + FText::Format(
			LOCTEXT("PasteGuidRegenerated", "{0} 个 GUID 与现有数据冲突，已重新生成。"), Result.RegeneratedGuids).ToString();
	}

	if (bKeepShared && SharedCount > 0 && UAssetFavoritesSettings::Get()->bHideSharedCategories)
	{
		Message += TEXT("\n") + LOCTEXT("PasteSharedHidden", "共享组已粘贴，但当前设置隐藏了公共收藏组。").ToString();
	}

	bool bWarning = false;
	if (Result.bRegistryLoading)
	{
		Message += TEXT("\n") + LOCTEXT("PasteRegistryLoading", "资产注册表仍在加载，未检查路径有效性。").ToString();
	}
	else if (Result.MissingPaths.Num() > 0)
	{
		bWarning = true;
		Message += TEXT("\n") + FText::Format(
			LOCTEXT("PasteMissing", "当前工程找不到 {0} 项（已保留为无效项）："), Result.MissingPaths.Num()).ToString();

		for (int32 i = 0; i < Result.MissingPaths.Num(); ++i)
		{
			if (i < MaxMissingPathsInNotification)
			{
				Message += TEXT("\n  ") + Result.MissingPaths[i];
			}
			UE_LOG(LogTemp, Warning, TEXT("AssetFavorites: Paste - not found in this project: %s"), *Result.MissingPaths[i]);
		}

		if (Result.MissingPaths.Num() > MaxMissingPathsInNotification)
		{
			Message += TEXT("\n  ") + FText::Format(
				LOCTEXT("PasteMissingMore", "...另有 {0} 项，见 Output Log"),
				Result.MissingPaths.Num() - MaxMissingPathsInNotification).ToString();
		}
	}

	ShowNotification(FText::FromString(Message), bWarning, bWarning ? 10.0f : 6.0f);
}

void FAssetFavoritesTransfer::ReloadAll()
{
	const FFavoritesReloadReport Report = FAssetFavoritesDataManager::Get().ReloadAll();

	auto DescribeFile = [](bool bReloaded, bool bMissing, bool bParseFailed) -> FText
	{
		if (bParseFailed)
		{
			return LOCTEXT("ReloadFileParseFailed", "解析失败，已保留内存数据");
		}
		if (bMissing)
		{
			return LOCTEXT("ReloadFileMissing", "文件缺失，已保留内存数据");
		}
		return bReloaded
			? LOCTEXT("ReloadFileReloaded", "已更新并重新加载")
			: LOCTEXT("ReloadFileUnchanged", "无变化");
	};

	FString Message = FText::Format(
		LOCTEXT("ReloadFiles", "个人配置：{0}\n共享配置：{1}"),
		DescribeFile(Report.bLocalReloaded, Report.bLocalMissing, Report.bLocalParseFailed),
		DescribeFile(Report.bSharedReloaded, Report.bSharedMissing, Report.bSharedParseFailed)).ToString();

	if (Report.bStatusSkipped)
	{
		Message += TEXT("\n") + LOCTEXT("ReloadStatusSkipped", "资产注册表仍在加载，已跳过条目状态检查。").ToString();
	}
	else
	{
		Message += TEXT("\n") + FText::Format(
			LOCTEXT("ReloadStatus", "条目状态：有效 {0} / 路径变动 {1} / 无效 {2}\n本次变为有效 {3}，变为无效 {4}，重新扫描 {5} 个文件"),
			Report.ValidCount, Report.MovedCount, Report.InvalidCount,
			Report.BecameValid, Report.BecameInvalid, Report.ScannedFiles).ToString();
	}

	const bool bWarning = Report.bLocalParseFailed || Report.bSharedParseFailed
		|| Report.bLocalMissing || Report.bSharedMissing
		|| Report.bStatusSkipped || Report.BecameInvalid > 0;

	ShowNotification(FText::FromString(Message), bWarning, bWarning ? 8.0f : 5.0f);
}

#undef LOCTEXT_NAMESPACE
