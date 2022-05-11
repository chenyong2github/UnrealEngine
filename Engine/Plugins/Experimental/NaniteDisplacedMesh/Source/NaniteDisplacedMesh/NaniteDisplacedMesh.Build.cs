// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NaniteDisplacedMesh : ModuleRules
	{
		public NaniteDisplacedMesh(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("NaniteDisplacedMesh/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"RenderCore",
					"RHI",
					"SlateCore",
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"DerivedDataCache",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"TargetPlatform",
					}
				);
			}
		}
	}
}
