// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Re2 : ModuleRules
{
	public Re2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}

		AddVcPackage("re2", true, "re2");
	}
}
