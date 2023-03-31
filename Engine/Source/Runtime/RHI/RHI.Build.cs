// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;

public class RHI : ModuleRules
{
	public RHI(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("TraceLog");
		PrivateDependencyModuleNames.Add("ApplicationCore");

		if (Target.bCompileAgainstEngine)
		{
			if (Target.Type == TargetRules.TargetType.Server)
			{
				// Dedicated servers should skip loading everything but NullDrv
				DynamicallyLoadedModuleNames.Add("NullDrv");
			}
			else
			{
				if (Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop))
                {
					PublicDefinitions.Add("RHI_WANT_BREADCRUMB_EVENTS=1");
					DynamicallyLoadedModuleNames.Add("NullDrv");
				}

				if (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test)
                {
					PublicDefinitions.Add("RHI_WANT_RESOURCE_INFO=1");
				}
				
				if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
				{
					DynamicallyLoadedModuleNames.Add("D3D11RHI");
					DynamicallyLoadedModuleNames.Add("D3D12RHI");
					DynamicallyLoadedModuleNames.Add("OpenGLDrv");
					DynamicallyLoadedModuleNames.Add("VulkanRHI");
				}

				if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
				{
					DynamicallyLoadedModuleNames.Add("VulkanRHI");
					DynamicallyLoadedModuleNames.Add("OpenGLDrv");
				}
			}
		}

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(new string[] { "ProfileVisualizer" });
		}
    }
}
