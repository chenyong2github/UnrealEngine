// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class BaseTextureBuildWorkerTarget : DerivedDataBuildWorkerTarget
{
	public BaseTextureBuildWorkerTarget(TargetInfo Target) : base(Target)
	{
		LaunchModuleName = "BaseTextureBuildWorker";
	}
}
