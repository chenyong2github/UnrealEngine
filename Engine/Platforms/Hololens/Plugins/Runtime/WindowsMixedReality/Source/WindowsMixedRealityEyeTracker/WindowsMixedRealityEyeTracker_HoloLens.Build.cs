// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityEyeTracker_HoloLens : WindowsMixedRealityEyeTracker
	{
		protected override bool bWithWindowsMixedReality { get => true; }

		public WindowsMixedRealityEyeTracker_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
		}
	}
}
