// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GoogleOboe : ModuleRules
{
	public GoogleOboe(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core"
                }
            );

        if (Target.Platform != UnrealTargetPlatform.Lumin)
        {
            Type = ModuleType.CPlusPlus;

            

            PublicIncludePaths.Add(Target.UEThirdPartySourceDirectory + "GoogleOboe/Public");
            PublicDefinitions.Add("WITH_GOOGLEOBOE=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_GOOGLEOBOE=0");
        }
    }
}
