// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CQTestTests : ModuleRules
{
	public CQTestTests(ReadOnlyTargetRules Target)
		: base(Target)
	{
        PrivateDependencyModuleNames.AddRange(
			new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"CQTest"
				 }
			);
	}
}
