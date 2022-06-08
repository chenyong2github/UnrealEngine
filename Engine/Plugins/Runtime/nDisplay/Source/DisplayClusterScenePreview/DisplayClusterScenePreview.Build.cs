// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterScenePreview : ModuleRules
{
	public DisplayClusterScenePreview(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"DisplayClusterLightCardEditorShaders",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"DisplayCluster",
				"DisplayClusterConfiguration",

				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore"
			});
	}
}
