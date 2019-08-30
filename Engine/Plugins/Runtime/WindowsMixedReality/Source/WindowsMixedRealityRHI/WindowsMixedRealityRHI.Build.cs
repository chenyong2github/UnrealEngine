// Copyright (c) Microsoft Corporation. All rights reserved.

using System;
using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
    public class WindowsMixedRealityRHI : ModuleRules
    {
        private string ModulePath
        {
            get { return ModuleDirectory; }
        }

        private string ThirdPartyPath
        {
            get { return Path.GetFullPath(Path.Combine(ModulePath, "../ThirdParty")); }
        }

        private void LoadMixedReality(ReadOnlyTargetRules Target)
        {
            PublicSystemLibraries.Add("DXGI.lib");
        }

        public WindowsMixedRealityRHI(ReadOnlyTargetRules Target) : base(Target)
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
                        "ProceduralMeshComponent"
                    }
                );

                PrivateDependencyModuleNames.AddRange(
                    new string[]
                    {
                        "Core",
                        "CoreUObject",
                        "ApplicationCore",
                        "Engine",
                        "InputCore",
                        "RHI",
                        "RenderCore",
                        "Renderer",
						//"ShaderCore",
						"Projects",
                        "HeadMountedDisplay",
                        "D3D11RHI",
                        "Slate",
                        "SlateCore",
                        "UtilityShaders",
                        "MRMesh",
                    }
                    );

                AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

                if (Target.bBuildEditor == true)
                {
                    PrivateDependencyModuleNames.Add("UnrealEd");
                }

                LoadMixedReality(Target);

                PrivateIncludePaths.AddRange(
                    new string[]
                    {
                    "WindowsMixedRealityRHI/Private",
                    "../../../../Source/Runtime/Windows/D3D11RHI/Private",
                    "../../../../Source/Runtime/Renderer/Private",
                    });

                //TODO: needed?
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
            }
        }
    }
}
