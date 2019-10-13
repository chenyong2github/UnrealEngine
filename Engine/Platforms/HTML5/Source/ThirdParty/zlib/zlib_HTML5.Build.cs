// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class zlib_HTML5 : zlib
{
	public zlib_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		string OldzlibPathInc = Path.Combine(Target.UEThirdPartySourceDirectory, "zlib", OldZlibVersion);
		string OldzlibPathLib = Path.Combine(Target.HTML5Platform.PlatformThirdPartySourceDirectory, "zlib", OldZlibVersion);

		PublicIncludePaths.Add(Path.Combine(OldzlibPathInc, "Inc"));
		PublicAdditionalLibraries.Add(Path.Combine(OldzlibPathLib, "zlib" + Target.HTML5OptimizationSuffix + ".bc"));
	}
}
