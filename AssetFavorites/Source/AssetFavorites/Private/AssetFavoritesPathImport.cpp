// Copyright LoF Team. All Rights Reserved.

#include "AssetFavoritesPathImport.h"
#include "AssetFavoritesDataManager.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AssetFavoritesPathImport"

DEFINE_LOG_CATEGORY_STATIC(LogAssetFavorites, Log, All);

namespace AssetFavoritesPathImportPrivate
{
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

	FString TrimAndUnquote(const FString& InLine)
	{
		FString Line = InLine;
		Line.TrimStartAndEndInline();
		if (Line.Len() >= 2)
		{
			const TCHAR First = Line[0];
			const TCHAR Last = Line[Line.Len() - 1];
			if ((First == TEXT('"') && Last == TEXT('"')) || (First == TEXT('\'') && Last == TEXT('\'')))
			{
				Line.MidInline(1, Line.Len() - 2);
				Line.TrimStartAndEndInline();
			}
		}
		return Line;
	}

	bool IsIgnorableLine(const FString& Line)
	{
		if (Line.IsEmpty())
		{
			return true;
		}
		if (Line[0] == TEXT('#'))
		{
			return true;
		}
		// 纯分隔线：-- 或 ==========
		bool bAllDash = true;
		for (const TCHAR Ch : Line)
		{
			if (Ch != TEXT('-') && Ch != TEXT('='))
			{
				bAllDash = false;
				break;
			}
		}
		return bAllDash;
	}

	FString StripAssetExtension(const FString& InPath)
	{
		FString Path = InPath;
		Path.RemoveFromEnd(TEXT(".uasset"), ESearchCase::IgnoreCase);
		Path.RemoveFromEnd(TEXT(".umap"), ESearchCase::IgnoreCase);
		Path.RemoveFromEnd(TEXT("/"));
		return Path;
	}

	FString MakeObjectPathFromPackage(const FString& PackagePath)
	{
		FString Path = PackagePath;
		Path.RemoveFromEnd(TEXT("/"));
		const FString ShortName = FPackageName::GetShortName(Path);
		if (ShortName.Contains(TEXT(".")))
		{
			return Path;
		}
		return Path + TEXT(".") + ShortName;
	}

	bool LooksLikeUnrealPath(const FString& Path)
	{
		// UE 包/对象/虚拟路径以单个 / 开头；P4 depot 以 // 开头
		return Path.StartsWith(TEXT("/")) && !Path.StartsWith(TEXT("//"));
	}

	bool TryConvertContentRootToPackage(const FString& NormalizedPath, FString& OutPackagePath, bool& bOutDirectory)
	{
		const FString ContentMarker = TEXT("/Content/");
		const int32 ContentIdx = NormalizedPath.Find(ContentMarker, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (ContentIdx == INDEX_NONE)
		{
			return false;
		}

		const FString Prefix = NormalizedPath.Left(ContentIdx);
		FString Rest = NormalizedPath.Mid(ContentIdx + ContentMarker.Len());

		const bool bHadAssetExt =
			Rest.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase)
			|| Rest.EndsWith(TEXT(".umap"), ESearchCase::IgnoreCase);
		bOutDirectory = Rest.EndsWith(TEXT("/")) && !bHadAssetExt;
		Rest = StripAssetExtension(Rest);

		if (Rest.IsEmpty())
		{
			return false;
		}

		if (!bHadAssetExt && !Rest.Contains(TEXT(".")))
		{
			bOutDirectory = true;
		}

		FString Mount = TEXT("/Game");
		if (Prefix.EndsWith(TEXT("/Engine")))
		{
			Mount = TEXT("/Engine");
		}
		else
		{
			const FString PluginsMarker = TEXT("/Plugins/");
			const int32 PluginsIdx = Prefix.Find(PluginsMarker, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (PluginsIdx != INDEX_NONE)
			{
				const FString PluginName = Prefix.Mid(PluginsIdx + PluginsMarker.Len());
				if (!PluginName.IsEmpty() && !PluginName.Contains(TEXT("/")))
				{
					Mount = TEXT("/") + PluginName;
				}
			}
		}

		OutPackagePath = Mount + TEXT("/") + Rest;
		FPaths::RemoveDuplicateSlashes(OutPackagePath);
		return true;
	}

	FGuid ResolveOrCreateTargetCategory(const FGuid& Preferred)
	{
		FAssetFavoritesDataManager& Manager = FAssetFavoritesDataManager::Get();
		Manager.LoadData();

		if (Preferred.IsValid() && Manager.FindCategoryById(Preferred))
		{
			return Preferred;
		}

		for (const FFavoritesCategory& Category : Manager.GetCategories())
		{
			if (Category.ConfigSource == EFavoritesConfigSource::Local)
			{
				return Category.UniqueId;
			}
		}

		if (Manager.GetCategories().Num() > 0)
		{
			return Manager.GetCategories()[0].UniqueId;
		}

		return Manager.AddCategory(TEXT("Default"), EFavoritesConfigSource::Local).UniqueId;
	}

	void ApplyParsedPaths(const FGuid& CategoryId, const FString& RawText)
	{
		TArray<FString> Lines;
		RawText.ParseIntoArrayLines(Lines, false);

		TArray<TPair<FString, EFavoritesItemType>> ToAdd;
		TArray<FString> FailedLines;
		TArray<FString> MissingPaths;
		TSet<FString> SeenInBatch;

		for (const FString& RawLine : Lines)
		{
			FString CanonicalPath;
			EFavoritesItemType ItemType = EFavoritesItemType::Asset;
			if (!FAssetFavoritesPathImport::TryParseRawPath(RawLine, CanonicalPath, ItemType))
			{
				const FString Trimmed = TrimAndUnquote(RawLine);
				if (!Trimmed.IsEmpty() && !IsIgnorableLine(Trimmed) && !SeenInBatch.Contains(Trimmed))
				{
					SeenInBatch.Add(Trimmed);
					FailedLines.Add(Trimmed);
				}
				continue;
			}

			bool bMissing = false;
			if (ItemType == EFavoritesItemType::Asset)
			{
				const FAssetData Resolved = FAssetFavoritesDataManager::ResolveAssetData(CanonicalPath, true);
				if (Resolved.IsValid())
				{
					CanonicalPath = Resolved.GetSoftObjectPath().ToString();
				}
				else
				{
					bMissing = true;
				}
			}

			if (SeenInBatch.Contains(CanonicalPath))
			{
				continue;
			}
			SeenInBatch.Add(CanonicalPath);

			if (bMissing)
			{
				MissingPaths.Add(CanonicalPath);
			}

			ToAdd.Emplace(CanonicalPath, ItemType);
		}

		if (ToAdd.Num() == 0 && FailedLines.Num() == 0)
		{
			ShowNotification(LOCTEXT("BatchAddEmpty", "没有可添加的路径。"), true, 4.0f);
			return;
		}

		const FFavoritesAddItemsResult Result = FAssetFavoritesDataManager::Get().AddItems(CategoryId, ToAdd);

		FString Message = FText::Format(
			LOCTEXT("BatchAddSummary", "批量添加完成：新增 {0} 项，重复跳过 {1} 项。"),
			Result.Added, Result.SkippedDuplicate).ToString();

		bool bWarning = false;
		if (FailedLines.Num() > 0)
		{
			bWarning = true;
			Message += TEXT("\n") + FText::Format(
				LOCTEXT("BatchAddFailed", "无法解析 {0} 行："), FailedLines.Num()).ToString();
			for (int32 i = 0; i < FailedLines.Num(); ++i)
			{
				if (i < MaxMissingPathsInNotification)
				{
					Message += TEXT("\n  ") + FailedLines[i];
				}
				UE_LOG(LogAssetFavorites, Warning, TEXT("AssetFavorites: BatchAdd - failed to parse: %s"), *FailedLines[i]);
			}
			if (FailedLines.Num() > MaxMissingPathsInNotification)
			{
				Message += TEXT("\n  ") + FText::Format(
					LOCTEXT("BatchAddFailedMore", "...另有 {0} 行，见 Output Log"),
					FailedLines.Num() - MaxMissingPathsInNotification).ToString();
			}
		}

		if (MissingPaths.Num() > 0)
		{
			bWarning = true;
			Message += TEXT("\n") + FText::Format(
				LOCTEXT("BatchAddMissing", "当前工程找不到 {0} 项（已保留为无效项）："), MissingPaths.Num()).ToString();
			for (int32 i = 0; i < MissingPaths.Num(); ++i)
			{
				if (i < MaxMissingPathsInNotification)
				{
					Message += TEXT("\n  ") + MissingPaths[i];
				}
				UE_LOG(LogAssetFavorites, Warning, TEXT("AssetFavorites: BatchAdd - not found: %s"), *MissingPaths[i]);
			}
			if (MissingPaths.Num() > MaxMissingPathsInNotification)
			{
				Message += TEXT("\n  ") + FText::Format(
					LOCTEXT("BatchAddMissingMore", "...另有 {0} 项，见 Output Log"),
					MissingPaths.Num() - MaxMissingPathsInNotification).ToString();
			}
		}

		ShowNotification(FText::FromString(Message), bWarning, bWarning ? 10.0f : 6.0f);
	}
}

using AssetFavoritesPathImportPrivate::TrimAndUnquote;
using AssetFavoritesPathImportPrivate::IsIgnorableLine;
using AssetFavoritesPathImportPrivate::StripAssetExtension;
using AssetFavoritesPathImportPrivate::MakeObjectPathFromPackage;
using AssetFavoritesPathImportPrivate::LooksLikeUnrealPath;
using AssetFavoritesPathImportPrivate::TryConvertContentRootToPackage;
using AssetFavoritesPathImportPrivate::ResolveOrCreateTargetCategory;
using AssetFavoritesPathImportPrivate::ApplyParsedPaths;

bool FAssetFavoritesPathImport::TryParseRawPath(const FString& RawLine, FString& OutPath, EFavoritesItemType& OutType)
{
	FString Line = TrimAndUnquote(RawLine);
	if (IsIgnorableLine(Line))
	{
		return false;
	}

	auto FinishAsset = [&OutPath, &OutType](const FString& PackageOrObject) -> bool
	{
		FString PackagePath = StripAssetExtension(PackageOrObject);
		FPaths::RemoveDuplicateSlashes(PackagePath);
		if (PackagePath.IsEmpty())
		{
			return false;
		}
		OutType = EFavoritesItemType::Asset;
		OutPath = MakeObjectPathFromPackage(PackagePath);
		return !OutPath.IsEmpty();
	};

	auto FinishDirectory = [&OutPath, &OutType](const FString& DirectoryPath) -> bool
	{
		FString Path = StripAssetExtension(DirectoryPath);
		FPaths::RemoveDuplicateSlashes(Path);
		if (Path.IsEmpty())
		{
			return false;
		}
		OutType = EFavoritesItemType::Directory;
		OutPath = Path;
		return true;
	};

	// 1. Copy Reference：Class'/Game/Path/Asset.Asset'（与 GetExportTextName 一致）
	{
		FString ClassName;
		FString ObjectPath;
		if (FPackageName::ParseExportTextPath(Line, &ClassName, &ObjectPath) && !ObjectPath.IsEmpty())
		{
			return FinishAsset(ObjectPath);
		}

		const int32 FirstQuote = Line.Find(TEXT("'"));
		const int32 LastQuote = Line.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (FirstQuote != INDEX_NONE && LastQuote > FirstQuote + 1)
		{
			const FString Quoted = Line.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
			if (LooksLikeUnrealPath(Quoted))
			{
				return FinishAsset(Quoted);
			}
		}
	}

	const FString NativePath = Line;
	Line.ReplaceInline(TEXT("\\"), TEXT("/"));

	const bool bHadAssetExt =
		Line.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase)
		|| Line.EndsWith(TEXT(".umap"), ESearchCase::IgnoreCase);

	// 2. Copy Object Path：/Game/Path/Asset.Asset
	// 3. Copy Package Path：/Game/Path/Asset
	if (LooksLikeUnrealPath(Line))
	{
		if (Line.StartsWith(TEXT("/All/")))
		{
			return FinishDirectory(Line);
		}
		return FinishAsset(Line);
	}

	// 4. Copy File Path：磁盘绝对路径（平台分隔符，通常是 .uasset）
	if (!Line.StartsWith(TEXT("//")))
	{
		FString LongPackageName;
		const bool bConverted =
			FPackageName::TryConvertFilenameToLongPackageName(NativePath, LongPackageName)
			|| FPackageName::TryConvertFilenameToLongPackageName(Line, LongPackageName);
		if (bConverted)
		{
			if (!bHadAssetExt && (NativePath.EndsWith(TEXT("\\")) || NativePath.EndsWith(TEXT("/")) || FPaths::DirectoryExists(NativePath)))
			{
				return FinishDirectory(LongPackageName);
			}
			return FinishAsset(LongPackageName);
		}
	}

	// P4 depot 或未能 round-trip 的磁盘路径：按 .../Content/... 映射到 /Game
	FString PackagePath;
	bool bDirectory = false;
	if (!TryConvertContentRootToPackage(Line, PackagePath, bDirectory))
	{
		return false;
	}
	return bDirectory ? FinishDirectory(PackagePath) : FinishAsset(PackagePath);
}

void FAssetFavoritesPathImport::ShowDialog(const FGuid& CategoryId)
{
	const FGuid TargetCategoryId = ResolveOrCreateTargetCategory(CategoryId);

	FString InitialText;
	{
		FString Clipboard;
		FPlatformApplicationMisc::ClipboardPaste(Clipboard);
		Clipboard.TrimStartAndEndInline();
		if (!Clipboard.IsEmpty()
			&& !Clipboard.Contains(TEXT("AssetFavorites.Categories"))
			&& (Clipboard.Contains(TEXT("/Game"))
				|| Clipboard.Contains(TEXT("/Engine"))
				|| Clipboard.Contains(TEXT("/Content/"))
				|| Clipboard.Contains(TEXT("\\Content\\"))
				|| Clipboard.Contains(TEXT("//"))
				|| Clipboard.Contains(TEXT(".uasset"))
				|| Clipboard.Contains(TEXT(".umap"))
				|| Clipboard.Contains(TEXT("'"))))
		{
			InitialText = Clipboard;
		}
	}

	struct FDialogState
	{
		TSharedPtr<SMultiLineEditableTextBox> TextBox;
		TWeakPtr<SWindow> Window;
	};
	const TSharedRef<FDialogState> State = MakeShared<FDialogState>();

	FString CategoryName = TEXT("Default");
	FString SourceLabel = TEXT("本地");
	if (const FFavoritesCategory* TargetCategory = FAssetFavoritesDataManager::Get().FindCategoryById(TargetCategoryId))
	{
		if (!TargetCategory->Name.IsEmpty())
		{
			CategoryName = TargetCategory->Name;
		}
		if (TargetCategory->ConfigSource == EFavoritesConfigSource::Shared)
		{
			SourceLabel = TEXT("公共");
		}
	}

	const FText Hint = LOCTEXT("BatchAddHint",
		"每行一条，可混贴。兼容收藏夹 / Content Browser 的 4 种复制路径：\n"
		"• Copy Reference：AnimMontage'/Game/.../Asset.Asset'\n"
		"• Copy Object Path：/Game/.../Asset.Asset\n"
		"• Copy Package Path：/Game/.../Asset\n"
		"• Copy File Path：磁盘绝对路径（...\\Asset.uasset）\n"
		"也支持 P4 depot：//ContentLoF/master/ProjectLoF/Content/.../Asset.uasset");

	const FText WindowTitle = FText::Format(
		LOCTEXT("BatchAddTitleFmt", "批量添加资产路径 — {0}"),
		FText::FromString(CategoryName));

	const TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(760.0f, 460.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::UserSized)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BatchAddTargetPrefix", "目标分组："))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::FromString(CategoryName))
						.ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.1f))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("BatchAddTargetSource", "（{0}）"), FText::FromString(SourceLabel)))
						.ColorAndOpacity(FLinearColor(1.0f, 0.85f, 0.1f))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(STextBlock)
					.Text(Hint)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(State->TextBox, SMultiLineEditableTextBox)
					.Text(FText::FromString(InitialText))
					.HintText(LOCTEXT("BatchAddTextHint", "在此粘贴路径，每行一条"))
					.AutoWrapText(false)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 8, 0, 0)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 8, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("BatchAddOk", "添加"))
						.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
						.OnClicked_Lambda([TargetCategoryId, State]()
						{
							if (State->TextBox.IsValid())
							{
								ApplyParsedPaths(TargetCategoryId, State->TextBox->GetPlainText().ToString());
							}
							if (const TSharedPtr<SWindow> Window = State->Window.Pin())
							{
								Window->RequestDestroyWindow();
							}
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("BatchAddCancel", "取消"))
						.OnClicked_Lambda([State]()
						{
							if (const TSharedPtr<SWindow> Window = State->Window.Pin())
							{
								Window->RequestDestroyWindow();
							}
							return FReply::Handled();
						})
					]
				]
			]
		];

	State->Window = DialogWindow;
	const TSharedPtr<SWindow> ParentWindow = FGlobalTabmanager::Get()->GetRootWindow();
	FSlateApplication::Get().AddModalWindow(DialogWindow, ParentWindow, false);
}

#undef LOCTEXT_NAMESPACE
