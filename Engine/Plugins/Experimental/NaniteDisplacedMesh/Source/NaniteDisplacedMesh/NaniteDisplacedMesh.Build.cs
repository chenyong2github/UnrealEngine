// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NaniteDisplacedMesh : ModuleRules
	{
		public NaniteDisplacedMesh(ReadOnlyTargetRules Target) : base(Target)
		{
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

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"DerivedDataCache",
						"MeshDescription",
						"StaticMeshDescription",
						"RawMesh",
						"MeshUtilities",
						"MeshUtilitiesCommon",
						"MeshBuilderCommon",
						"MeshBuilder",
						"NaniteUtilities",
					}
				);

				PrivateIncludePathModuleNames.Add("NaniteBuilder");
				DynamicallyLoadedModuleNames.Add("NaniteBuilder");
			}
		}
	}
}
