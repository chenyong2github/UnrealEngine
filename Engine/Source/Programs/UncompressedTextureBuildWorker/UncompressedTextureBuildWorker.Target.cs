// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class UncompressedTextureBuildWorkerTarget : DerivedDataBuildWorkerTarget
{
	public UncompressedTextureBuildWorkerTarget(TargetInfo Target) : base(Target)
	{
		LaunchModuleName = "UncompressedTextureBuildWorker";
	}
}
