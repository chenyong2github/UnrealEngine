// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DatasmithRuntime : ModuleRules
{
	public DatasmithRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("Private");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"DatasmithContent",
				"DatasmithCore",
				"DatasmithNativeTranslator",
				"DatasmithTranslator",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"DirectLink",
				"Engine",
				"IESFile",
				"FreeImage",
				"Landscape",
				"LevelSequence",
				"MeshDescription",
				"MeshUtilitiesCommon",
				"RawMesh",
				"RHI",
				"RenderCore",
				"SlateCore",
				"StaticMeshDescription",
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"DesktopPlatform",
					"MessageLog",
				}
			);
		}
	}
}