// Copyright (c) Microsoft Corporation. All rights reserved.

using System;
using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
    public class WindowsMixedRealityRHI : ModuleRules
    {
		protected virtual bool bSupportedPlatform { get => Target.Platform == UnrealTargetPlatform.Win64; }

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

            if (bSupportedPlatform)
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
						"RHICore",
                        "Slate",
                        "SlateCore",
                        "MRMesh",
						"WindowsMixedRealityHMD",
                    }
                    );

                AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

				if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
				{
					AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
					AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
				}

				if (Target.bBuildEditor == true)
                {
					PrivateDependencyModuleNames.Add("EditorFramework");
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
                if (Target.Platform == UnrealTargetPlatform.Win64)
                {
                    PrivateIncludePaths.Add("../../../../Source/Runtime/Windows/D3D11RHI/Private/Windows");
                }

                PCHUsage = PCHUsageMode.NoSharedPCHs;
                PrivatePCHHeaderFile = "Private/WindowsMixedRealityPrecompiled.h";
            }
        }
    }
}
