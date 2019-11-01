// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class Vorbis_HTML5 : Vorbis
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }

	public Vorbis_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicAdditionalLibraries.Add(VorbisLibPath + "/libvorbis" + Target.HTML5OptimizationSuffix + ".bc");
	}
}
