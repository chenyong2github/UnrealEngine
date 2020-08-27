// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UHTLite : ModuleRules
{
	public UHTLite(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Json",
				"Projects"
			}
		);
	
		PrivateIncludePaths.AddRange(
			new string[]
			{
				// For LaunchEngineLoop.cpp include
				"Runtime/Launch/Private",
				"Programs/UHTLite/Private",
			});
		
		bEnableExceptions = true;

		UnsafeTypeCastWarningLevel = WarningLevel.Warning;
	}
}
