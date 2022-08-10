// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityRHI_HoloLens : WindowsMixedRealityRHI
	{
		protected override bool bSupportedPlatform { get => true; }

		public WindowsMixedRealityRHI_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("../../../../Platforms/HoloLens/Source/Runtime/Windows/D3D11RHI/Private");
		}
	}
}
