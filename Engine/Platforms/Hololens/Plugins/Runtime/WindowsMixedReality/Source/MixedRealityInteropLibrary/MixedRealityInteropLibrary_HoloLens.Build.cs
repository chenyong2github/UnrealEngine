// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using EpicGames.Core;

namespace UnrealBuildTool.Rules
{
	public class MixedRealityInteropLibrary_HoloLens : MixedRealityInteropLibrary
	{
		public MixedRealityInteropLibrary_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			string InteropLibPath = Target.UEThirdPartySourceDirectory + "/WindowsMixedRealityInterop/Lib/" + Target.WindowsPlatform.GetArchitectureSubpath() + "/";
			bool bUseDebugInteropLibrary = Target.Configuration == UnrealTargetConfiguration.Debug && Target.Architecture == "ARM64";
			if (Target.Configuration == UnrealTargetConfiguration.Debug && !bUseDebugInteropLibrary)
			{
				Log.TraceInformation("Building Hololens Debug {0} but not using MixedRealityInteropHoloLensDebug.lib due to Debug Win CRT incompatibilities.  Debugging within the interop will be similar to release.", Target.Architecture); // See also WindowsMixedRealityInterop.Build.cs
			}
			PublicAdditionalLibraries.Add(InteropLibPath + (bUseDebugInteropLibrary ? "MixedRealityInteropHoloLensDebug.lib" : "MixedRealityInteropHoloLens.lib"));

		}
	}
}
