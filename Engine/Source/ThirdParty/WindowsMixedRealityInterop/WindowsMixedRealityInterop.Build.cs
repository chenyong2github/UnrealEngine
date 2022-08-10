// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class WindowsMixedRealityInterop : ModuleRules
{
	protected string WMRIPath { get => Path.Combine(Target.UEThirdPartySourceDirectory, "WindowsMixedRealityInterop"); }
	protected string LibrariesPath { get => Path.Combine(WMRIPath, "Lib", Architecture); }


	protected virtual bool bWithWMRI { get => Target.Platform == UnrealTargetPlatform.Win64; }
	protected virtual string Architecture { get => "x64"; }


	public WindowsMixedRealityInterop(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string IncludePath = Path.Combine(WMRIPath, "Include");
        PublicIncludePaths.Add(IncludePath);

        if (bWithWMRI && Target.Platform == UnrealTargetPlatform.Win64)
        {
			//HACK: use the release version of the interop because the debug build isn't compatible with UE right now.
            //if (Target.Configuration == UnrealTargetConfiguration.Debug)
            //{
            //    PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInteropDebug.lib"));
            //}
            //else
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "MixedRealityInterop.lib"));
            }

            PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=1");
        }
        else if (!bWithWMRI)
		{
            PublicDefinitions.Add("WITH_SCENE_UNDERSTANDING=0");
        }

        if (bWithWMRI)
        {
            // Win10 support
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "onecore.lib"));
            // Explicitly load lib path since name conflicts with an existing lib in the DX11 dependency.
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "d3d11.lib"));
        }
	}
}

