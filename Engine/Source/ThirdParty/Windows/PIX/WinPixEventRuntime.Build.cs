// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class WinPixEventRuntime : ModuleRules
{
	public WinPixEventRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64 || Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
		{
			string WinPixDir = Path.Combine(Target.UEThirdPartySourceDirectory, "Windows/PIX");

			PublicSystemIncludePaths.Add(Path.Combine( WinPixDir, "include") );

            PublicDelayLoadDLLs.Add("WinPixEventRuntime.dll");
            PublicAdditionalLibraries.Add( Path.Combine( WinPixDir, "Lib/" + Target.WindowsPlatform.Architecture.ToString() + "/WinPixEventRuntime.lib") );
            RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/WinPixEventRuntime/" + Target.WindowsPlatform.Architecture.ToString() + "/WinPixEventRuntime.dll");
        }
	}
}

