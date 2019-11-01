// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class SDL2_HTML5 : SDL2
{
	protected override string LibRootDirectory { get { return Target.HTML5Platform.PlatformThirdPartySourceDirectory; } }

	public SDL2_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicAdditionalLibraries.Add(SDL2LibPath + "/libSDL2" + Target.HTML5OptimizationSuffix + ".bc");
	}
}
