// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UEOpenExr : ModuleRules
{
	private enum EXRVersion
	{
		EXR_2_3_0,
	}
	public UEOpenExr(ReadOnlyTargetRules Target) : base(Target)
	{
		// Set which version to use.
		EXRVersion Version = EXRVersion.EXR_2_3_0;

		string DeployDir = "openexr/Deploy/";
		if (Version == EXRVersion.EXR_2_3_0)
		{
			DeployDir += "OpenEXR-2.3.0";
		}

		Type = ModuleType.External;
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) || Target.Platform == UnrealTargetPlatform.Mac || Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);
			string LibDir = Target.UEThirdPartySourceDirectory + DeployDir + "/OpenEXR//lib/";
			string Platform = "";
			if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
			{
				Platform = "x64";
				LibDir += "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				Platform = "Mac";
				bDebug = false;
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				Platform = "Unix";
				bDebug = false;
			}
			LibDir = LibDir + "/" + Platform;
			LibDir = LibDir + "/Static" + (bDebug ? "Debug" : "Release");

			if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
			{
				PublicAdditionalLibraries.AddRange(
					new string[] {
						LibDir + "/Half.lib",
						LibDir + "/Iex.lib",
						LibDir + "/IexMath.lib",
						LibDir + "/IlmImf.lib",
						LibDir + "/IlmImfUtil.lib",
						LibDir + "/IlmThread.lib",
						LibDir + "/Imath.lib",
					}
				);
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.AddRange(
					new string[] {
						LibDir + "/libHalf.a",
						LibDir + "/libIex.a",
						LibDir + "/libIexMath.a",
						LibDir + "/libIlmImf.a",
						LibDir + "/libIlmImfUtil.a",
						LibDir + "/libIlmThread.a",
						LibDir + "/libImath.a",
					}
				);
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture.StartsWith("x86_64"))
			{
				string LibArchDir = LibDir + "/" + Target.Architecture;
				PublicAdditionalLibraries.AddRange(
					new string[] {
						LibArchDir + "/libHalf.a",
						LibArchDir + "/libIex.a",
						LibArchDir + "/libIexMath.a",
						LibArchDir + "/libIlmImf.a",
						LibArchDir + "/libIlmImfUtil.a",
						LibArchDir + "/libIlmThread.a",
						LibArchDir + "/libImath.a",
					}
				);
			}

			PublicSystemIncludePaths.AddRange(
				new string[] {
					Target.UEThirdPartySourceDirectory + DeployDir + "/OpenEXR/include",
					Target.UEThirdPartySourceDirectory + DeployDir + "/OpenEXR/include/openexr", // Alembic SDK is non-standard, doesn't prefix its include correctly
				}
			);
		}
	}
}
