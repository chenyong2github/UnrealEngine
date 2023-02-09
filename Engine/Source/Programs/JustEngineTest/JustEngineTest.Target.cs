// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class JustEngineTestTarget : TestTargetRules
{
	public JustEngineTestTarget(TargetInfo Target) : base(Target)
	{
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		
		bBuildWithEditorOnlyData = true;
		bBuildRequiresCookedData = false;
		
		bCompileAgainstEditor = false;
		bCompileAgainstEngine = true;
	}
}
