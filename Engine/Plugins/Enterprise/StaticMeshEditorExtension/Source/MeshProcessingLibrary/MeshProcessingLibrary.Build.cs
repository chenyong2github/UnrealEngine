// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class MeshProcessingLibrary : ModuleRules
{
	public MeshProcessingLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;
		bLegalToDistributeObjectCode = true;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"MainFrame",
				"MeshDescription",
				"Slate",
				"SlateCore",
				"StaticMeshDescription",
				"UnrealEd",
			}
		);

		bool bWithProxyLOD = Target.Platform == UnrealTargetPlatform.Win64;
		PrivateDefinitions.Add("WITH_PROXYLOD=" + (bWithProxyLOD ? '1' : '0'));
		if (bWithProxyLOD)
		{
			// For boost:: and TBB:: code
			bEnableUndefinedIdentifierWarnings = false;
			bUseRTTI = true;

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ProxyLODMeshReduction",
				}
			);
		}

		// Setup MeshSimplifier
		string MeshSimplifierPath = Path.Combine(EngineDirectory, "Restricted/NotForLicensees/Source/ThirdParty/Enterprise/MeshSimplifier");
		bool bWithMeshSimplifier = Directory.Exists(MeshSimplifierPath) && Target.Platform == UnrealTargetPlatform.Win64;
		PublicDefinitions.Add("WITH_MESH_SIMPLIFIER=" + (bWithMeshSimplifier ? '1' : '0'));

		if (bWithMeshSimplifier)
		{
			PrivateIncludePaths.Add(MeshSimplifierPath + "/Include");
			PublicAdditionalLibraries.Add(MeshSimplifierPath + "/lib/x64/MeshSimplifier.lib");
		}
	}
}
