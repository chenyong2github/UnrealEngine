// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterColorGrading : ModuleRules
{
	public DisplayClusterColorGrading(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"DisplayClusterOperator",

				"AppFramework",
				"ColorCorrectRegions",
				"Core",
				"CoreUObject",
				"DetailCustomizations",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Kismet",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"UnrealEd",
				"WorkspaceMenuStructure",
			});
	}
}
