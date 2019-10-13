// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class FreeType2_HTML5 : FreeType2
{
	protected override string FreeType2Version
	{
		get
		{
			return "FreeType2-2.6";
		}
	}

	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }
	protected override string FreeType2LibPath { get { return Path.GetDirectoryName(base.FreeType2LibPath); } }

	public FreeType2_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// library to link
		PublicAdditionalLibraries.Add(Path.Combine(FreeType2LibPath, "libfreetype260" + Target.HTML5OptimizationSuffix + ".bc"));
	}
}
