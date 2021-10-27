// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ToolWidgets : ModuleRules
{
	public ToolWidgets(ReadOnlyTargetRules Target) : base(Target)
	{
		/** NOTE: THIS MODULE SHOULD NOT EVER DEPEND ON UNREALED. 
		 * If you are adding a reusable widget that depends on UnrealEd, add it to EditorWidgets instead
		 */
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Slate",
				"SlateCore",
				"InputCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Developer/ToolWidgets/Private",
			}
		);
	}
}
