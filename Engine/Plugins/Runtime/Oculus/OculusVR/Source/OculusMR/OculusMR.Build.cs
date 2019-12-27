// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class OculusMR : ModuleRules
	{
		public OculusMR(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"InputDevice",			// For IInputDevice.h
					"HeadMountedDisplay",	// For IMotionController.h
					"ImageWrapper",
					"Engine"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
                    "Slate",
                    "SlateCore",
                    "RHI",
                    "VulkanRHI",
                    "RenderCore",
                    "MediaAssets",
                    "HeadMountedDisplay",
					"OculusHMD",
					"OculusInput",
                    "OVRPlugin"
                });

			PrivateIncludePaths.AddRange(
				new string[] {
					// Relative to Engine\Plugins\Runtime\Oculus\OculusVR\Source
					"OculusHMD/Private",
                    "OculusInput/Private",
                    "../../../../../Source/Runtime/Renderer/Private",
					"../../../../../Source/Runtime/Engine/Classes/Components",
                    "../../../../../Source/Runtime/MediaAssets/Private",
                });

            if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
            {
				PublicDelayLoadDLLs.Add("OVRPlugin.dll");
				RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Oculus/OVRPlugin/OVRPlugin/" + Target.Platform.ToString() + "/OVRPlugin.dll");
			}

			if (Target.Platform == UnrealTargetPlatform.Android)
            {
                PrivateIncludePaths.Add("../../../../../Source/Runtime/VulkanRHI/Private");
                PrivateIncludePaths.Add("../../../../../Source/Runtime/VulkanRHI/Private/" + Target.Platform);
                AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
            }

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
            }
        }
	}
}
