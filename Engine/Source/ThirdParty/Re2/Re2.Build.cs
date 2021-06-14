// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Re2 : ModuleRules
{
	public Re2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			// Currently only supported for Win64
			return;
		}

		AddVcPackage(Target, "re2", true, "re2");
	}
}
