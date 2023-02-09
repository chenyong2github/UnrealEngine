// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using UnrealBuildTool;

public class JustEngineTest : TestModuleRules
{
	public JustEngineTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Engine"
			});
	}
}