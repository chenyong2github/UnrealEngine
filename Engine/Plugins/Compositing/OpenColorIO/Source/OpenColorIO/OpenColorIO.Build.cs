// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	using System.IO;

	public class OpenColorIO : ModuleRules
	{
		public OpenColorIO(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"Projects",
					"RenderCore",
					"RHI",
					"RenderCore",
					"Renderer",
					"Slate",
					"SlateCore",
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					//required for FPostProcessMaterialInputs
					"../../../../Source/Runtime/Renderer/Private",
				});

			PublicDependencyModuleNames.AddRange(
				new string[] {
				});

			if (Target.bBuildEditor == true)
			{
				if (Target.Platform == UnrealTargetPlatform.Win64 ||
						Target.Platform == UnrealTargetPlatform.Win32)
				{
					//There are some dynamic_cast in OCIO library.
					bUseRTTI = true;
				}

				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"DerivedDataCache",
						"OpenColorIOLib",
						"TargetPlatform",
						"UnrealEd"
					});
			}
		}
	}
}
