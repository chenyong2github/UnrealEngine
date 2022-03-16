// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CurveExpressionEditor : ModuleRules
{
	public CurveExpressionEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"AnimationCore",
				"Core",
				"CoreUObject",
				"CurveExpression",
				"Engine",
				
				// UI 
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"RewindDebuggerInterface",
				"EditorStyle",
				"UnrealEd",
				"InputCore"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph"
			}
		);
	}
}
