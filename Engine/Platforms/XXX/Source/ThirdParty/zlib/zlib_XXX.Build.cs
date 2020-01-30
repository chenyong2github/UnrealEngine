// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;

public class zlib_XXX : zlib
{
	public zlib_XXX(ReadOnlyTargetRules Target) : base(Target)
	{
		// find my location
		DirectoryReference ModuleDir = GetModuleDirectoryForSubClass(typeof(zlib_XXX));
		
		string ZlibDir = Path.Combine(ModuleDir.FullName, CurrentZlibVersion);
		PublicIncludePaths.Add(ZlibDir + "/inc");
		PublicAdditionalLibraries.Add(Path.Combine(ZlibDir, "Lib/XXX/libz.a"));
	}
}
