// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class HarfBuzz_HTML5 : HarfBuzz
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }

	public HarfBuzz_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// Can't be used without our dependencies
		if (!Target.bCompileFreeType || !Target.bCompileICU)
		{
			return;
		}

		// base added 0 for unknown platform
		PublicDefinitions.Remove("WITH_HARFBUZZ=0");
		PublicDefinitions.Add("WITH_HARFBUZZ=1");

		// library to link
		PublicAdditionalLibraries.Add(Path.Combine(LibHarfBuzzRootPath, "libharfbuzz" + Target.HTML5OptimizationSuffix + ".bc"));
	}
}
