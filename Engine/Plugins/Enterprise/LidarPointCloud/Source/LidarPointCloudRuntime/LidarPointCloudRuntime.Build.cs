// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

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

			// Currently, E57 is only supported on Windows
			bool bSupportLibE57 = true;

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Source", "ThirdParty", "LasZip", "Win64", "laszip.dll"));
				if(bSupportLibE57)
				{
					RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Source", "ThirdParty", "LibE57", "Win64", "xerces-c_3_2.dll"));
					RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Source", "ThirdParty", "LibE57", "Win64", "E57UE4.dll"));
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Source", "ThirdParty", "LasZip", "Mac", "laszip.dylib"));
				bSupportLibE57 = false;
			}
			else
			{
				bSupportLibE57 = false;
			}

			AppendStringToPublicDefinition("LIBE57SUPPORTED", bSupportLibE57 ? "1" : "0");
		}
	}
}
