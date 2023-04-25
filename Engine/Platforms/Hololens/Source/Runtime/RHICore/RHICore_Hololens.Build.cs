// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RHICore_HoloLens : RHICore
{
	public RHICore_HoloLens(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDefinitions.Add("RHICORE_PLATFORM_DXGI_H=<dxgi.h>");
	}
}
