// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class HoloLensAR : ModuleRules
{
    public HoloLensAR(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Engine",
				"MixedRealityInteropLibrary",
				"MRMesh"
				// ... add other public dependencies that you statically link with here ...
			}
			);

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
				"ApplicationCore",
                "HeadMountedDisplay",
                "AugmentedReality",
                "RenderCore",
                "RHI",
				"Projects",
				// ... add private dependencies that you statically link with here ...
			}
            );
			
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.MixedReality.QR.dll"));
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.MixedReality.QR.winmd"));
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.MixedReality.SceneUnderstanding.dll"));
            RuntimeDependencies.Add(System.IO.Path.Combine("$(EngineDir)/Binaries/ThirdParty/Windows/x64", "Microsoft.MixedReality.SceneUnderstanding.winmd"));
            PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
            foreach (var Dll in Directory.EnumerateFiles(Path.Combine(Target.UEThirdPartyBinariesDirectory, "Windows/x64"), "*_app.dll"))
            {
                RuntimeDependencies.Add(Dll);
            }

			PrivateDependencyModuleNames.Add("D3D11RHI");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
        }

		AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

		PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
    }
}
