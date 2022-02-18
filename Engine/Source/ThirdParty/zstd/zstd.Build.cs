// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class zstd : ModuleRules
{
	public zstd(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddVcPackage("zstd", true, "zstd_static");
		}
		else
		{
			AddVcPackage("zstd", true, "zstd");
		}
	}
}
