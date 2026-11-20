// Copyright LoF Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 收藏夹分类的复制 / 粘贴 / 重新加载（均为静态入口，供右键菜单调用）。
 *
 * 复制只复制分类和条目（含共享/个人标记、条目的 UniqueId），不拷贝任何资产文件。
 * 载荷是一段 JSON 文本，直接放在系统剪贴板里。
 */
class FAssetFavoritesTransfer
{
public:
	/** 复制指定分类到剪贴板（按显示顺序输出，忽略不存在的 ID） */
	static void CopyCategories(const TArray<FGuid>& CategoryIds);

	/** 剪贴板里是否像是收藏夹分类数据（用于菜单置灰，只做轻量判断） */
	static bool ClipboardHasFavorites();

	/**
	 * 从剪贴板粘贴分类。
	 * @param AnchorCategoryId 右键所在的分类；无效表示在面板空白处右键，追加到对应来源末尾
	 */
	static void PasteFromClipboard(const FGuid& AnchorCategoryId);

	/** 重新加载：检查两个 JSON 是否更新，并刷新全部条目的有效状态 */
	static void ReloadAll();
};
