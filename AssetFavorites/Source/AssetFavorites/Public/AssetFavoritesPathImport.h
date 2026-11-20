// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetFavoritesTypes.h"

/**
 * 从多行文本批量导入资产 / 目录路径到收藏夹。
 * 支持弹窗输入；路径格式包括 P4 depot、磁盘路径、/Game 包路径、对象路径、Copy Reference。
 */
class FAssetFavoritesPathImport
{
public:
	/**
	 * 弹出多行输入窗口，确认后把解析到的路径加入指定分类。
	 * CategoryId 无效时落到第一个本地分类；没有任何分类则新建 Default。
	 */
	static void ShowDialog(const FGuid& CategoryId);

	/** 解析一行原始路径。成功时写出收藏夹使用的 Path 与类型。 */
	static bool TryParseRawPath(const FString& RawLine, FString& OutPath, EFavoritesItemType& OutType);
};
