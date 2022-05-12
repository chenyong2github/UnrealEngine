// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NaniteDisplacedMeshEditor : ModuleRules
	{
		public NaniteDisplacedMeshEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("NaniteDisplacedMeshEditor/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetTools",
					"Core",
					"CoreUObject",
					"EditorFramework",
					"EditorStyle",
					"Engine",
					"RHI",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"TargetPlatform",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"EditorSubsystem",
					"NaniteDisplacedMesh"
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"DerivedDataCache",
				}
			);
		}
	}
}
