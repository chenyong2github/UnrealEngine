// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosClothAssetDataflowNodes : ModuleRules
{
	public ChaosClothAssetDataflowNodes(ReadOnlyTargetRules Target) : base(Target)
	{	
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);	
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Chaos",
				"ChaosCloth",
				"ChaosClothAsset",
				"ChaosClothAssetEngine",
				"CoreUObject",
				"DataflowCore",
				"DataflowEditor",
				"DataflowEngine",
				"DatasmithImporter",
				"DynamicMesh",
				"Engine",
				"ExternalSource",
				"GeometryCore",
				"MeshConversion",
				"MeshDescription",
				"SkeletalMeshDescription",
				"Slate",
				// ... add private dependencies that you statically link with here ...	
			}
			);
	}
}
