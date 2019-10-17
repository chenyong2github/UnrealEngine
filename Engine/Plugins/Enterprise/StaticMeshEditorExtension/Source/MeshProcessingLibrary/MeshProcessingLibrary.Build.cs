// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class MeshProcessingLibrary : ModuleRules
{
	public MeshProcessingLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
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
				"MeshDescriptionOperations",
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
		string MeshSimplifierPath = Path.Combine(ModuleDirectory, "ThirdParty/NotForLicensees/MeshSimplifier");
		bool bWithMeshSimplifier = Directory.Exists(MeshSimplifierPath) && Target.Platform == UnrealTargetPlatform.Win64;
		PublicDefinitions.Add("WITH_MESH_SIMPLIFIER=" + (bWithMeshSimplifier ? '1' : '0'));

		if (bWithMeshSimplifier)
		{
			PrivateIncludePaths.Add(MeshSimplifierPath + "/Include");

			bool bUseMeshSimplifierVC141 = Target.WindowsPlatform.Compiler == WindowsCompiler.VisualStudio2017;
			string libSuffix = bUseMeshSimplifierVC141 ? "_vc141" : "";
			PublicAdditionalLibraries.Add(MeshSimplifierPath + "/lib/x64/MeshSimplifier" + libSuffix + ".lib");
		}
	}
}
