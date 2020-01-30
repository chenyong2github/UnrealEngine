// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UEOpenExr : ModuleRules
{
	private enum EXRVersion
	{
		EXR_1_7_1,
		EXR_2_3_0,
	}
    public UEOpenExr(ReadOnlyTargetRules Target) : base(Target)
    {
		// Set which version to use.
		EXRVersion Version = EXRVersion.EXR_2_3_0;
		string DeployDir = "openexr/Deploy/";
		if (Version == EXRVersion.EXR_1_7_1)
		{
			DeployDir += "OpenEXR-1.7.1";
		}
		else
		{
			DeployDir += "OpenEXR-2.3.0";
		}

        Type = ModuleType.External;
		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Mac || Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
        {
            bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);
            string LibDir = Target.UEThirdPartySourceDirectory + DeployDir + "/OpenEXR//lib/";
			string Platform = "";
			if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                    Platform = "x64";
                    LibDir += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";
			}
			else if (Target.Platform == UnrealTargetPlatform.Win32)
			{
                    Platform = "Win32";
                    LibDir += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
                    Platform = "Mac";
                    bDebug = false;
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
                    Platform = "Linux";
                    bDebug = false;
            }
            LibDir = LibDir + "/" + Platform;
            LibDir = LibDir + "/Static" + (bDebug ? "Debug" : "Release");

			if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
			{
				PublicAdditionalLibraries.AddRange(
					new string[] {
						LibDir + "/Half.lib",
						LibDir + "/Iex.lib",
						LibDir + "/IlmImf.lib",
						LibDir + "/IlmThread.lib",
						LibDir + "/Imath.lib",
					}
				);

				if (Version >= EXRVersion.EXR_2_3_0)
				{
					PublicAdditionalLibraries.AddRange(
						new string[] {
							LibDir + "/IexMath.lib",
							LibDir + "/IlmImfUtil.lib",
						}
					);
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.AddRange(
					new string[] {
						LibDir + "/libHalf.a",
						LibDir + "/libIex.a",
						LibDir + "/libIlmImf.a",
						LibDir + "/libIlmThread.a",
						LibDir + "/libImath.a",
					}
				);
                if (Version >= EXRVersion.EXR_2_3_0)
                {
                    PublicAdditionalLibraries.AddRange(
                        new string[] {
                            LibDir + "/libIexMath.a",
                            LibDir + "/libIlmImfUtil.a",
                        }
                    );
                }
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture.StartsWith("x86_64"))
			{
				string LibArchDir = LibDir + "/" + Target.Architecture;
				PublicAdditionalLibraries.AddRange(
					new string[] {
						LibArchDir + "/libHalf.a",
						LibArchDir + "/libIex.a",
						LibArchDir + "/libIlmImf.a",
						LibArchDir + "/libIlmThread.a",
						LibArchDir + "/libImath.a",
					}
				);
				if (Version >= EXRVersion.EXR_2_3_0)
				{
				    PublicAdditionalLibraries.AddRange(
				        new string[] {
				            LibArchDir + "/libIexMath.a",
				            LibArchDir + "/libIlmImfUtil.a",
				        }
				    );
				}
			}

            PublicSystemIncludePaths.AddRange(
                new string[] {
                    Target.UEThirdPartySourceDirectory + DeployDir,
					Target.UEThirdPartySourceDirectory + DeployDir + "/OpenEXR/include",
				}
            );
        }
    }
}

