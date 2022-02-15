// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class http_parser : ModuleRules
{
	public http_parser(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}

		AddVcPackage("http-parser", true, "http_parser");
	}
}
