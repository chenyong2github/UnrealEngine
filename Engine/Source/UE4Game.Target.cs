// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class UE4GameTarget : TargetRules
{
	public UE4GameTarget( TargetInfo Target ) : base(Target)
	{
		Type = TargetType.Game;
		BuildEnvironment = TargetBuildEnvironment.Shared;

		ExtraModuleNames.Add("UE4Game");

		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			// to make iOS projects as small as possible we excluded some items from the engine.
			// uncomment below to make a smaller iOS build
			/*bCompileRecast = false;
			bCompileSpeedTree = false;
			bCompileAPEX = false;
			bCompileLeanAndMeanUE = true;
			bCompilePhysXVehicle = false;
			bCompileFreeType = false;
			bCompileForSize = true;*/
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			if (Target.Configuration == UnrealTargetConfiguration.Shipping)
			{
				bUseLoggingInShipping = true;
			}
		}
	}
}
