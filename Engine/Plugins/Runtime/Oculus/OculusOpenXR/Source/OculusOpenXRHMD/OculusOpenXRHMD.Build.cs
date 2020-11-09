// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class OculusOpenXRHMD : ModuleRules
    {
        public OculusOpenXRHMD(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
                    // Relative to Engine\Plugins\Runtime\Oculus\OculusOpenXR\Source
                    "../../../OpenXR/Source/OpenXRHMD/Private",
                    "../../../../../Source/Runtime/Renderer/Private",
                    "../../../../../Source/Runtime/OpenGLDrv/Private",
                    "../../../../../Source/Runtime/VulkanRHI/Private",
                    "../../../../../Source/Runtime/Engine/Classes/Components",
                    "../../../../../Source/Runtime/Engine/Classes/Kismet",
                });

            PublicIncludePathModuleNames.AddRange(
                new string[] {
                    "Launch",
                    "OpenXRHMD",
                });			

            if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
            {
                PrivateIncludePaths.Add("../../../../../Source/Runtime/VulkanRHI/Private/Windows");
            }
            else
            {
                PrivateIncludePaths.Add("../../../../../Source/Runtime/VulkanRHI/Private/" + Target.Platform);
            }

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "InputCore",
                    "RHI",
                    "RenderCore",
                    "Renderer",
                    "Slate",
                    "SlateCore",
                    "ImageWrapper",
                    "MediaAssets",
                    "Analytics",
                    "OpenGLDrv",
                    "VulkanRHI",
                    "HeadMountedDisplay",
                    "OculusOpenXRLoader",
                    "Projects",
                });
            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "OpenXRHMD",
                });

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
            }

            AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");

            if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
            {
                // D3D
                {
                    PrivateDependencyModuleNames.AddRange(
                        new string[]
                        {
                            "D3D11RHI",
                            "D3D12RHI",
                        });

                    PrivateIncludePaths.AddRange(
                        new string[]
                        {
                            "../../../../../Source/Runtime/Windows/D3D11RHI/Private",
                            "../../../../../Source/Runtime/Windows/D3D11RHI/Private/Windows",
                            "../../../../../Source/Runtime/D3D12RHI/Private",
                            "../../../../../Source/Runtime/D3D12RHI/Private/Windows",
                        });

                    AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAPI");
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11Audio");
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectSound");
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
                }

                // Vulkan
                {
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
                }
            }
            else if (Target.Platform == UnrealTargetPlatform.Android)
            {
                PrivateIncludePaths.AddRange(
                        new string[]
                        {
                        });

                // Vulkan
                {
                    AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
                }

                // AndroidPlugin
                {
                    string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
                    AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "OculusMobile_APL.xml"));
                }
            }
        }
    }
}
