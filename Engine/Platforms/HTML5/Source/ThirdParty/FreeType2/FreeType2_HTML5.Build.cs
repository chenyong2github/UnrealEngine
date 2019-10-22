// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class FreeType2_HTML5 : FreeType2
{
	protected override string FreeType2Version { get { return "FreeType2-2.10.0"; } }
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }

	public FreeType2_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "libfreetype" + Target.HTML5OptimizationSuffix + ".bc"));
	}
}
