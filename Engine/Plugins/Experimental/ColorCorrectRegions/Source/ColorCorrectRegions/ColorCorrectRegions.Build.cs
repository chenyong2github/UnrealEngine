// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ColorCorrectRegions : ModuleRules
{
	public ColorCorrectRegions(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				//Used in ColorCorrectRegionsPostProcessMaterial.h
				Path.Combine(GetModuleDirectory("Renderer"), "Private"),
			}
		);
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"RenderCore",
				"DisplayClusterLightCardExtender"
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MovieSceneCapture",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"Renderer",
				"Projects",
				"RenderCore"
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PublicDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("EditorWidgets");
		}
	}
}
