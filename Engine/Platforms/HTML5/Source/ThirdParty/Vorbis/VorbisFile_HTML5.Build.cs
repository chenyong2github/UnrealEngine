// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class VorbisFile_HTML5 : VorbisFile
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string VorbisFileLibPath { get { return Path.GetDirectoryName(base.VorbisFileLibPath); } }

	public VorbisFile_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicAdditionalLibraries.Add(VorbisFileLibPath + "/libvorbisfile" + Target.HTML5OptimizationSuffix + ".bc");
	}
}

