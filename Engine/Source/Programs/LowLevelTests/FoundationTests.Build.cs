// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using UnrealBuildTool;

public class FoundationTests : TestModuleRules
{
	public FoundationTests(ReadOnlyTargetRules Target) : base(Target, true)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Cbor",
				"CoreUObject"
			});

		if (Target.bBuildWithEditorOnlyData)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"DesktopPlatform"
				});
		}

		UpdateBuildGraphPropertiesFile(new Metadata("Foundation"), false);
	}
}