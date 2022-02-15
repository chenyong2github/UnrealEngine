// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class fmt : ModuleRules
{
	public fmt(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}

		AddVcPackage("fmt", true, "fmt");
	}
}
