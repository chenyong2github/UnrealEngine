// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HTTP_HTML5 : HTTP
{
	public HTTP_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("HTML5JS");
	}
}
