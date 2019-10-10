// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UEOgg_HTML5 : UEOgg
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string OggLibPath { get { return Path.GetDirectoryName(base.OggLibPath); } } // strip off the lib

	public UEOgg_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// library to link
		PublicAdditionalLibraries.Add(OggLibPath + "/libogg" + Target.HTML5OptimizationSuffix + ".bc");
	}
}
