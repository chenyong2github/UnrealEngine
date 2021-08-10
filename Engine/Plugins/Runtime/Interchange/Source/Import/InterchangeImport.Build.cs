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
					"IESFile",
					"ImageWrapper",
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
						"UnrealEd",
					}
				);
			}
		}
	}
}
