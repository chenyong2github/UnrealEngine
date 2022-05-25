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
		protected virtual bool bSupportedPlatform { get => Target.Platform == UnrealTargetPlatform.Win64; }

		private string ModulePath
		{
			get { return ModuleDirectory; }
		}
	 
		private void LoadMixedReality(ReadOnlyTargetRules Target)
        {
            //// Set a macro allowing us to switch between debuggame/development configuration
            //HACK: use the release version of the interop because the debug build isn't compatible with UE right now.
            //if (Target.Configuration == UnrealTargetConfiguration.Debug)
            //{
            //    PrivateDefinitions.Add("WINDOWS_MIXED_REALITY_DEBUG_DLL=1");
            //}
            //else
            {
                PrivateDefinitions.Add("WINDOWS_MIXED_REALITY_DEBUG_DLL=0");
            }

			if(Target.Platform == UnrealTargetPlatform.Win64)
            {
				// HoloLens 2 Remoting
                RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/x64/Microsoft.Holographic.AppRemoting.dll");
                RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/x64/PerceptionDevice.dll");
            }

            PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
        }
        
        public WindowsMixedRealityHMD(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableExceptions = true;

			if (bSupportedPlatform)
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

				if (bSupportedPlatform)
				{
					PrivateDependencyModuleNames.Add("HoloLensAR");
				}

				if (Target.bBuildEditor == true)
				{
					PrivateDependencyModuleNames.Add("EditorFramework");
					PrivateDependencyModuleNames.Add("UnrealEd");
                    PrivateDependencyModuleNames.Add("WindowsMixedRealityRuntimeSettings");
                }

				AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

                LoadMixedReality(Target);

                PrivateIncludePaths.AddRange(
					new string[]
					{
					"WindowsMixedRealityHMD/Private",
					"../../../../Source/Runtime/Renderer/Private",
					});

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
