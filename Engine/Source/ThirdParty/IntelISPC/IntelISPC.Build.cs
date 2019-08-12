// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelISPC : ModuleRules
{
	public IntelISPC(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if ( ( Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 ) && Target.WindowsPlatform.StaticAnalyzer != WindowsStaticAnalyzer.PVSStudio )
		{
            PublicDefinitions.Add("INTEL_ISPC=1");
        }
		else
        {
            PublicDefinitions.Add("INTEL_ISPC=0");
        }
	}
}