// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformFileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"

/**
 * AssetFavorites 内部使用的 Source Control 工具函数。
 * 直接从 EditorEx/SourceControlUtils.h 提取必要功能，使 AssetFavorites 成为自包含插件。
 */
struct FAssetFavoritesSourceControlUtils
{
	/**
	 * 确保指定文件可写。如果文件不存在或已经可写，直接返回 true。
	 * 如果文件只读，按以下顺序尝试：
	 *   1. 通过 UE Source Control 进行 checkout
	 *   2. 直接去除文件只读属性
	 */
	static bool EnsureFileWritable(const FString& FilePath)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		if (!PlatformFile.FileExists(*FilePath) || !PlatformFile.IsReadOnly(*FilePath))
		{
			return true;
		}

		UE_LOG(LogTemp, Warning, TEXT("AssetFavorites: File is read-only: %s, attempting to make writable..."), *FilePath);

		if (ISourceControlModule::Get().IsEnabled())
		{
			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			FSourceControlStatePtr State = Provider.GetState(FilePath, EStateCacheUsage::ForceUpdate);
			if (State.IsValid() && State->CanCheckout())
			{
				if (Provider.Execute(ISourceControlOperation::Create<FCheckOut>(), FilePath) == ECommandResult::Succeeded)
				{
					UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Checked out file from source control: %s"), *FilePath);
					if (!PlatformFile.IsReadOnly(*FilePath))
					{
						return true;
					}
				}
			}
		}

		if (PlatformFile.IsReadOnly(*FilePath))
		{
			if (PlatformFile.SetReadOnly(*FilePath, false))
			{
				UE_LOG(LogTemp, Log, TEXT("AssetFavorites: Removed read-only attribute from: %s"), *FilePath);
				return true;
			}
			UE_LOG(LogTemp, Error, TEXT("AssetFavorites: Cannot remove read-only attribute from: %s"), *FilePath);
			return false;
		}

		return true;
	}

	/**
	 * 获取文件的 Source Control 状态图标（小图标）。
	 */
	static const FSlateBrush* GetSourceControlIconForFile(const FString& FilePath)
	{
		if (!ISourceControlModule::Get().IsEnabled())
		{
			return nullptr;
		}

		ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
		if (!Provider.IsEnabled())
		{
			return nullptr;
		}

		FSourceControlStatePtr State = Provider.GetState(FilePath, EStateCacheUsage::Use);
		if (!State.IsValid())
		{
			return nullptr;
		}

		return State->GetIcon().GetSmallIcon();
	}
};
