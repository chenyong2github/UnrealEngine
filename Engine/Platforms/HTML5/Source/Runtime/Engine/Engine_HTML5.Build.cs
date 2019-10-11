// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Engine_HTML5 : Engine
{
	public Engine_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// remove some unsupported formats

		PublicDependencyModuleNames.Add("HTML5JS");
	}
}
