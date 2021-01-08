// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;
using System.Collections.Generic;
using Microsoft.Win32;
using System.Diagnostics;


namespace UnrealBuildTool.Rules
{
	public class AzureSpatialAnchorsForWMR : ModuleRules
	{
		public AzureSpatialAnchorsForWMR(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"MixedRealityInteropLibrary",
					"HoloLensAR",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AzureSpatialAnchors",
					"WindowsMixedRealityHMD",
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "CoarseRelocUW.dll"));
				RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.Azure.SpatialAnchors.dll"));
				RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.Azure.SpatialAnchors.winmd"));
			}
			else if (Target.Platform == UnrealTargetPlatform.HoloLens)
			{
				RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "CoarseRelocUW.dll"));
				RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.dll"));
				RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.winmd"));
			}

			PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
		}
	}
}
