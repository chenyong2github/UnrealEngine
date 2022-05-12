// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InterchangeImport : ModuleRules
	{
		public InterchangeImport(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InterchangeCore",
					"InterchangeDispatcher",
					"InterchangeEngine",
					"InterchangeFactoryNodes",
					"InterchangeNodes",
					"MeshDescription",
					"StaticMeshDescription",
					"SkeletalMeshDescription",
				}
			);
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetRegistry",
					"CinematicCamera",
					"GLTFCore",
					"IESFile",
					"ImageCore",
					"ImageWrapper",
					"InterchangeCommonParser",
					"InterchangeDispatcher",
					"InterchangeMessages",
					"Json",
					"RHI",
					"TextureUtilitiesCommon",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"BSPUtils",
						"MaterialEditor",
						"UnrealEd",
					}
				);
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target, "MaterialX");
		}
	}
}
