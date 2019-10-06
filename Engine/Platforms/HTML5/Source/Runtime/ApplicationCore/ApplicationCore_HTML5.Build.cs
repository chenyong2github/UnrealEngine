// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ApplicationCore_HTML5 : ApplicationCore
{
	public ApplicationCore_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");
		PrivateDependencyModuleNames.Add("HTML5JS");
		PrivateDependencyModuleNames.Add("HTML5MapPakDownloader");
	}
}
