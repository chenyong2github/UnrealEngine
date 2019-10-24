// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class HairStrandsEditor : ModuleRules
	{
		public HairStrandsEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add(ModuleDirectory + "/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"EditorStyle",
					"Engine",
					"HairStrandsCore",
					"InputCore",
					"MainFrame",
					"Slate",
					"SlateCore",
					"Projects",
					"UnrealEd",
					"AssetTools",
				});
			AddEngineThirdPartyPrivateStaticDependencies(Target,
			 "FBX"
			);
		}
	}
}
