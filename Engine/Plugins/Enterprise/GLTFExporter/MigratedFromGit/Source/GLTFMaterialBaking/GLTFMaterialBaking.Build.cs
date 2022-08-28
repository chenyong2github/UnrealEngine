// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GLTFMaterialBaking : ModuleRules
{
	public GLTFMaterialBaking(ReadOnlyTargetRules Target) : base(Target)
	{
		bTreatAsEngineModule = true;

		PublicDependencyModuleNames .AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string [] {
				"RenderCore",
				"RHI",
				"UnrealEd",
				"MeshDescription",
				"StaticMeshDescription",
			}
		);

		// NOTE: ugly hack to access HLSLMaterialTranslator to bake shading model
		PrivateIncludePaths.Add(EngineDirectory + "/Source/Runtime/Engine/Private");

		// NOTE: avoid renaming all instaces of MATERIALBAKING_API by redirecting it to GLTFMATERIALBAKING_API
		PublicDefinitions.Add("MATERIALBAKING_API=GLTFMATERIALBAKING_API");
	}
}
