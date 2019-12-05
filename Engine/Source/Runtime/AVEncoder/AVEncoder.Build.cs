// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class AVEncoder : ModuleRules
{
	public AVEncoder(ReadOnlyTargetRules Target) : base(Target)
	{

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Engine",
			"RHI",
			"RenderCore",
		});

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
				{
					"D3D11RHI"
				});

			PublicDelayLoadDLLs.Add("mfplat.dll");
			PublicDelayLoadDLLs.Add("mfuuid.dll");
			PublicDelayLoadDLLs.Add("Mfreadwrite.dll");

			PublicSystemLibraries.Add("d3d11.lib");
			PublicSystemLibraries.Add("DXGI.lib");
		}

	}
}

