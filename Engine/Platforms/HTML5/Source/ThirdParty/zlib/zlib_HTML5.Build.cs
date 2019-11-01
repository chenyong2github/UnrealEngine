// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class zlib_HTML5 : zlib
{
	public zlib_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		string zlibPath = Path.Combine(Target.HTML5Platform.PlatformThirdPartySourceDirectory, "zlib", CurrentZlibVersion);

		// v1.2.8
		PublicIncludePaths.Add(zlibPath + "/include");
		PublicAdditionalLibraries.Add(zlibPath + "/lib/zlib" + Target.HTML5OptimizationSuffix + ".bc");
	}
}
