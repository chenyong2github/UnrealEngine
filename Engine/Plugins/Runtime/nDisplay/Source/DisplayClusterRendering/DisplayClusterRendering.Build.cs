// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterRendering : ModuleRules
{
	public DisplayClusterRendering(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"DisplayCluster"
			});

		PrivateIncludePaths.AddRange(
			new string[] {
				"../../../../Source/Runtime/Renderer/Private"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"EditorWidgets",
				"Engine",
				"PropertyEditor",
				"Renderer",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore"
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
