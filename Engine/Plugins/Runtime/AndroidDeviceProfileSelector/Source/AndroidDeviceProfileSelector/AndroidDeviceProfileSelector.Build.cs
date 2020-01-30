// Copyright Epic Games, Inc. All Rights Reserved.
using Tools.DotNETCommon;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class AndroidDeviceProfileSelector : ModuleRules
	{
        public AndroidDeviceProfileSelector(ReadOnlyTargetRules Target) : base(Target)
		{
			ShortName = "AndroidDPS";

            string SecretFile = Path.Combine(ModuleDirectory, "Private", "NoRedist", "AndroidDeviceProfileSelectorSecrets.h");
			if (File.Exists(SecretFile))
            {
                PrivateDefinitions.Add("ANDROIDDEVICEPROFILESELECTORSECRETS_H=1");
            }
            else
            {
                PrivateDefinitions.Add("ANDROIDDEVICEPROFILESELECTORSECRETS_H=0");
            }

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				    "CoreUObject",
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				    "Core",
				    "CoreUObject",
				    "Engine",
				}
				);
		}
	}
}
