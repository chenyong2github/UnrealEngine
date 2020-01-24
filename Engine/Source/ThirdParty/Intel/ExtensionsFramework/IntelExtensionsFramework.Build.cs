// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelExtensionsFramework : ModuleRules
{
	public IntelExtensionsFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IntelExtensionsFrameworkPath = Target.UEThirdPartySourceDirectory + "Intel/" + "ExtensionsFramework/";

		if ( (Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32) )
		{
			PublicSystemIncludePaths.Add(IntelExtensionsFrameworkPath);

            PublicDefinitions.Add("INTEL_EXTENSIONS=0");
        }
        else
        {
            PublicDefinitions.Add("INTEL_EXTENSIONS=0");
        }
	}
}