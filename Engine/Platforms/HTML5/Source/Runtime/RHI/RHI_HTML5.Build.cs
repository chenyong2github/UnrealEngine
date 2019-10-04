// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;

public class RHI_HTML5 : RHI
{
	public RHI_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.bCompileAgainstEngine)
		{
			if (Target.Type != TargetRules.TargetType.Server)   // Dedicated servers should skip loading everything but NullDrv
			{
				DynamicallyLoadedModuleNames.Add("OpenGLDrv");
			}
		}
	}
}
