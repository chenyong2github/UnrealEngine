// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CLI11 : ModuleRules
{
	public CLI11(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}

		AddVcPackage("cli11", true, new string[] {});
	}
}
