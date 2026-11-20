// Copyright LoF Team. All Rights Reserved.

using UnrealBuildTool;

public class AssetFavorites : ModuleRules
{
	public AssetFavorites(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				ModuleDirectory + "/Public",
				ModuleDirectory + "/Public/Widgets",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				ModuleDirectory + "/Private",
				ModuleDirectory + "/Private/Widgets",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorSubsystem",
				"UnrealEd",
				"ContentBrowser",
				"AssetRegistry",
				"Json",
				"JsonUtilities",
				"EditorStyle",
				"InputCore",
				"ToolMenus",
				"Projects",
				"DeveloperSettings",
				"ContentBrowserData",
				"LevelEditor",
				"SourceControl",  // UE 内置 Source Control API
				"ApplicationCore",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
