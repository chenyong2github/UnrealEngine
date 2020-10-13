// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDUtilities : ModuleRules
	{
		public USDUtilities(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"UnrealUSDWrapper",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CinematicCamera",
					"DesktopPlatform",
					"EditorStyle",
					"Engine",
					"GeometryCache", // Just so that we can fetch its AssetImportData
					"IntelTBB",
					"MaterialBaking", // So that we can use the BakeMaterials function
					"MaterialEditor",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"MovieScene",
					"MovieSceneTracks",
					"PropertyEditor",
					"RenderCore",
					"RHI", // So that we can use GMaxRHIFeatureLevel when force-loading textures before baking materials
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UnrealEd",
					"USDClasses",
				}
				);
		}
	}
}
