// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatformsAttribute(new string[] {"HTML5"})]
public class OpenGLDrv_HTML5 : OpenGLDrv
{
	public OpenGLDrv_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		// base will set this, but we don't want it
		PrivateDependencyModuleNames.Remove("OpenGL");

		AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");

		// base will set this to None
		PrecompileForTargets = PrecompileTargetsType.Default;
	}
}
