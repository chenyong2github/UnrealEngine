// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealitySpatialInput_HoloLens : WindowsMixedRealitySpatialInput
	{
		public WindowsMixedRealitySpatialInput_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableExceptions = true;

			PCHUsage = PCHUsageMode.NoSharedPCHs;
			PrivatePCHHeaderFile = "Private/WindowsMixedRealitySpatialInput.h";
		}
	}
}
