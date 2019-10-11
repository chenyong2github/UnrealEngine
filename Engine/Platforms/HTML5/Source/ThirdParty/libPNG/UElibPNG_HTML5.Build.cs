// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UElibPNG_HTML5 : UElibPNG
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string LibPNGPath { get { return Path.GetDirectoryName(base.LibPNGPath); } }


	public UElibPNG_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicAdditionalLibraries.Add(Path.Combine(LibPNGPath, "libpng" + Target.HTML5OptimizationSuffix + ".bc"));
	}
}
