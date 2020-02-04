// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LidarPointCloudRuntime : ModuleRules
	{
		public LidarPointCloudRuntime(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"Slate",
					"SlateCore",
					"Core",
					"Engine",
					"RenderCore",
					"Projects",
					"RHI",
					"InputCore"
				}
            );

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"PropertyEditor",
						"EditorStyle",
						"ContentBrowser",
						"AssetRegistry"
					}
				);
			}
		}
	}
}