// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NNXRuntimeRDG : ModuleRules
{
	public NNXRuntimeRDG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[] 
		{ 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore",
			"RenderCore"
		});

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "NNXCore",
			"NNXUtils",
			"NNEHlslShaders",
            "RHI"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("D3D12RHI");
			PrivateDependencyModuleNames.Add("DirectMLDefault");

			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectMLDefault");
		}

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{	
			PrivateDependencyModuleNames.Add("MetalRHI");
		}

		if (Target.Platform == UnrealTargetPlatform.Linux)
		{	
			PrivateDependencyModuleNames.Add("VulkanRHI");
		}
	}
}
