// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class Core_HTML5 : Core
{
	public Core_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("HTML5JS");
		PrivateDependencyModuleNames.Add("HTML5MapPakDownloader");
	}
}
