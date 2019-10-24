// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ICU_HTML5 : ICU
{
	protected override string ICUVersion { get { return ICU64VersionString; } }
	protected override string ICULibRootPath { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string ICULibPath { get { return Path.Combine(ICULibRootPath, "ICU", ICUVersion, "lib"); } }

	public ICU_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicAdditionalLibraries.Add(ICULibPath + "/libicu" + Target.HTML5OptimizationSuffix + ".bc");
	}
}
