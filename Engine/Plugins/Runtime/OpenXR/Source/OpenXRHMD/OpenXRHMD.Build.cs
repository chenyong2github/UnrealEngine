// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Linq;
using EpicGames.Core;

namespace UnrealBuildTool.Rules
{
	public class OpenXRHMD : ModuleRules
	{
		public OpenXRHMD(ReadOnlyTargetRules Target) : base(Target)
        {
            var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            PrivateIncludePaths.AddRange(
				new string[] {
					"OpenXRHMD/Private",
                    EngineDir + "/Source/ThirdParty/OpenXR/include",
                    EngineDir + "/Source/Runtime/Renderer/Private",
					// ... add other private include paths required here ...
				}
				);

			PublicIncludePathModuleNames.Add("OpenXR");

            PublicDependencyModuleNames.Add("HeadMountedDisplay");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
                    "BuildSettings",
                    "InputCore",
					"RHI",
					"RenderCore",
					"Renderer",
					"RenderCore",
                    "Slate",
                    "SlateCore",
					"AugmentedReality",
					"EngineSettings",
				}
				);

			if (Target.bBuildEditor == true)
            {
				PrivateDependencyModuleNames.Add("EditorFramework");
                PrivateDependencyModuleNames.Add("UnrealEd");
			}

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                PrivateDependencyModuleNames.AddRange(new string[] {
					"D3D11RHI",
					"D3D12RHI"
				});

                AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11", "DX12");
            }

            if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Android)
            {
                PrivateDependencyModuleNames.Add("OpenGLDrv");

                AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenGL");
			}

			if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Android  
			    || Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
            {
                PrivateDependencyModuleNames.Add("VulkanRHI");

                AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
			}

			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				bool bAndroidOculusEnabled = false;
				if (Target.ProjectFile != null)
				{
					ProjectDescriptor Project = ProjectDescriptor.FromFile(Target.ProjectFile);
					if (Project.Plugins != null)
					{
						foreach (PluginReferenceDescriptor PluginDescriptor in Project.Plugins)
						{
							if ((PluginDescriptor.Name == "OculusVR") && PluginDescriptor.IsEnabledForPlatform(Target.Platform))
							{
								Log.TraceInformation("OpenXRHMD: Android Oculus plugin enabled, will use OculusMobile_APL.xml");
								bAndroidOculusEnabled = true;
								break;
							}
						}
					}
				}

				if (!bAndroidOculusEnabled)
				{
					Log.TraceInformation("OpenXRHMD: Using OculusOpenXRLoader_APL.xml");

					// If the Oculus plugin is not enabled we need to include our own APL
					string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
					AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "..", "..", "OculusOpenXRLoader_APL.xml"));
				}
			}
		}
	}
}
