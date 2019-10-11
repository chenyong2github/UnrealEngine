// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class libOpus_HTML5 : libOpus
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }

	public libOpus_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// library to link
		PublicAdditionalLibraries.Add(OpusLibPath + "/libopus" + Target.HTML5OptimizationSuffix + ".bc");
		PublicAdditionalLibraries.Add(OpusLibPath + "/libspeex_resampler" + Target.HTML5OptimizationSuffix + ".bc");
	}
}
