// Copyright (c) Microsoft Corporation. All rights reserved.

using System;
using System.IO;
using UnrealBuildTool;
using System.Collections.Generic;
using Microsoft.Win32;
using System.Diagnostics;


namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityHMD : ModuleRules
	{
		private string ModulePath
		{
			get { return ModuleDirectory; }
		}
	 
		private void LoadMixedReality(ReadOnlyTargetRules Target)
        {
            // Set a macro allowing us to switch between debuggame/development configuration
            if (Target.Configuration == UnrealTargetConfiguration.Debug)
            {
                PrivateDefinitions.Add("WINDOWS_MIXED_REALITY_DEBUG_DLL=1");
            }
            else
            {
                PrivateDefinitions.Add("WINDOWS_MIXED_REALITY_DEBUG_DLL=0");
            }

			if(Target.Platform == UnrealTargetPlatform.Win64)
            {
				// HoloLens 2 Remoting
                RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/x64/Microsoft.Holographic.AppRemoting.dll");
                RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/x64/PerceptionDevice.dll");
				
				// HoloLens 1 Remoting
                RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/HolographicStreamerDesktop.dll");
                RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/Microsoft.Perception.Simulation.dll");
                RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/PerceptionSimulationManager.dll");
            }

            PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
        }
        
        public WindowsMixedRealityHMD(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableExceptions = true;

			if (Target.Platform == UnrealTargetPlatform.Win32 ||
				Target.Platform == UnrealTargetPlatform.Win64 ||
				Target.Platform == UnrealTargetPlatform.HoloLens)
			{
				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"HeadMountedDisplay",
						"ProceduralMeshComponent",
						"MixedRealityInteropLibrary",
						"InputDevice",
					}
				);
			
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"Core",
						"CoreUObject",
						"ApplicationCore",
						"Engine",
                        "EngineSettings",
                        "InputCore",
						"RHI",
						"RenderCore",
						"Renderer",
						"HeadMountedDisplay",
						"D3D11RHI",
						"Slate",
						"SlateCore",
						"Projects",
						"AugmentedReality",
					}
					);

				if (Target.Platform == UnrealTargetPlatform.Win64 ||
					Target.Platform == UnrealTargetPlatform.HoloLens)
				{
					PrivateDependencyModuleNames.Add("HoloLensAR");
				}

				if (Target.bBuildEditor == true)
				{
					PrivateDependencyModuleNames.Add("UnrealEd");
                    PrivateDependencyModuleNames.Add("WindowsMixedRealityRuntimeSettings");
                }

				if (Target.Platform != UnrealTargetPlatform.HoloLens)
				{
					AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
					AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
				}

                AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

                LoadMixedReality(Target);

                PrivateIncludePaths.AddRange(
					new string[]
					{
					"WindowsMixedRealityHMD/Private",
					"../../../../Source/Runtime/Windows/D3D11RHI/Private",
					"../../../../Source/Runtime/Renderer/Private",
					});

				if (Target.Platform == UnrealTargetPlatform.Win32 ||
					Target.Platform == UnrealTargetPlatform.Win64)
				{
					PrivateIncludePaths.Add("../../../../Source/Runtime/Windows/D3D11RHI/Private/Windows");
				}
				else if (Target.Platform == UnrealTargetPlatform.HoloLens)
				{
					PrivateIncludePaths.Add("../../../../Source/Runtime/Windows/D3D11RHI/Private/HoloLens");
				}

				PCHUsage = PCHUsageMode.NoSharedPCHs;
				PrivatePCHHeaderFile = "Private/WindowsMixedRealityPrecompiled.h";

                if (Target.Platform == UnrealTargetPlatform.Win64)
                {
					RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "CoarseRelocUW.dll"));
					RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.Azure.SpatialAnchors.dll"));
                    RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.Azure.SpatialAnchors.winmd"));
					PublicDelayLoadDLLs.Add("CoarseRelocUW.dll");
					PublicDelayLoadDLLs.Add("Microsoft.Azure.SpatialAnchors.dll");
                    RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.MixedReality.QR.dll"));
                    PublicDelayLoadDLLs.Add("Microsoft.MixedReality.QR.dll");
                    RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.MixedReality.SceneUnderstanding.dll"));
                    PublicDelayLoadDLLs.Add("Microsoft.MixedReality.SceneUnderstanding.dll");
                    PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
					foreach(var Dll in Directory.EnumerateFiles(Path.Combine(Target.UEThirdPartyBinariesDirectory, "Windows/x64"), "*_app.dll"))
                    {
                        RuntimeDependencies.Add(Dll);
                    }
                }
                else if (Target.Platform == UnrealTargetPlatform.HoloLens)
                {
					RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "CoarseRelocUW.dll"));
					RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.dll"));
                    RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.Azure.SpatialAnchors.winmd"));
					PublicDelayLoadDLLs.Add("CoarseRelocUW.dll");
					PublicDelayLoadDLLs.Add("Microsoft.Azure.SpatialAnchors.dll");
                    PublicDelayLoadDLLs.Add("Microsoft.MixedReality.QR.dll");
                    RuntimeDependencies.Add(Path.Combine("$(EngineDir)/Binaries/ThirdParty/HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.QR.dll"));

                    string SceneUnderstandingPath = Path.Combine(Target.UEThirdPartyBinariesDirectory, "HoloLens", Target.WindowsPlatform.GetArchitectureSubpath(), "Microsoft.MixedReality.SceneUnderstanding.dll");
                    if (File.Exists(SceneUnderstandingPath))
                    {
                        RuntimeDependencies.Add(SceneUnderstandingPath);
                        PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
                    }
                    else
                    {
                        PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
                    }
				}
			}
			
			if (Target.Platform == UnrealTargetPlatform.Win64 && Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("WindowsMixedRealityInputSimulation");
				PrivateDefinitions.Add("WITH_INPUT_SIMULATION=1");
			}
			else
			{
				PrivateDefinitions.Add("WITH_INPUT_SIMULATION=0");
			}
		}
	}
}
