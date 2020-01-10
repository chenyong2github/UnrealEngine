// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionExampleCore : ModuleRules
	{
        public GeometryCollectionExampleCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("GeometryCollectionExampleCore/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
					"CoreUObject",
                    "GeometryCollectionCore",
                    "GeometryCollectionSimulationCore",
                    "GoogleTest",
                    "ChaosSolvers",
                    "Chaos",
                    "FieldSystemCore",
                    "FieldSystemSimulationCore"
                }
            );

			if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.XboxOne)
			{
				PublicDefinitions.Add("GTEST_OS_WINDOWS=1");
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicDefinitions.Add("GTEST_OS_MAC=1");
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS)
			{
				PublicDefinitions.Add("GTEST_OS_IOS=1");
			}
			else if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Quail || Target.Platform == UnrealTargetPlatform.Lumin)
			{
				PublicDefinitions.Add("GTEST_OS_LINUX_ANDROID=1");
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.PS4)
			{
				PublicDefinitions.Add("GTEST_OS_LINUX=1");
			}
        }
	}
}
