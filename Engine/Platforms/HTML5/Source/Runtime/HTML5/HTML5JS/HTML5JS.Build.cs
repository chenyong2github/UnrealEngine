// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using Tools.DotNETCommon;
//using System.Runtime.CompilerServices;

public class HTML5JS : ModuleRules
{
	// Does not depend on any Unreal modules.
	// UBT doesn't automatically understand .js code and the fact that it needs to be linked in or not.
	public HTML5JS(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicAdditionalLibraries.Add(Path.GetDirectoryName(__FILE__()) + "/Private/HTML5JavaScriptFx.js");
	}

	// https://stackoverflow.com/a/44139163
	static string __FILE__([System.Runtime.CompilerServices.CallerFilePath] string fileName = "")
	{
		return fileName;
	}
}
