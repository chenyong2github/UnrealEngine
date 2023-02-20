// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealHeaderTool : ModuleRules
{
	public UnrealHeaderTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"RHI",
				"RigVM",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Json",
				"Projects"
			}
		);
		
		bEnableExceptions = true;

		UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
