// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Linq;
using System.Text;
using System.IO;
using AutomationTool;
using UnrealBuildTool;
using EpicGames.Core;
using UnrealBuildBase;
using System.Diagnostics;


public abstract class ApplePlatform : Platform
{
	public ApplePlatform(UnrealTargetPlatform TargetPlatform)
		: base(TargetPlatform)
	{
	}

	#region SDK

	public override bool GetSDKInstallCommand(out string Command, out string Params, ref bool bRequiresPrivilegeElevation, ref bool bCreateWindow, ITurnkeyContext TurnkeyContext)
	{
		Command = "";
		Params = "";

		if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
		{
			TurnkeyContext.Log("Moving your original Xcode application from /Applications to the Trash, and unzipping the new version into /Applications");

			// put current Xcode in the trash, and unzip a new one. Xcode in the dock will have to be fixed up tho!
			Command = "osascript";
			Params =
				" -e \"try\"" +
				" -e   \"tell application \\\"Finder\\\" to delete POSIX file \\\"/Applications/Xcode.app\\\"\"" +
				" -e \"end try\"" +
				" -e \"do shell script \\\"cd /Applications; xip --expand $(CopyOutputPath);\\\"\"" +
				" -e \"try\"" +
				" -e   \"do shell script \\\"xcode-select -s /Applications/Xcode.app; xcode-select --install; xcodebuild -license accept; xcodebuild -runFirstLaunch\\\" with administrator privileges\"" +
				" -e \"end try\"";
		}
		else if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
		{

			TurnkeyContext.Log("Uninstalling old iTunes and preparing the new one to be installed.");

			Command = "$(EngineDir)/Extras/iTunes/Install_iTunes.bat";
			Params = "$(CopyOutputPath)";
		}
		return true;
	}

	public override bool OnSDKInstallComplete(int ExitCode, ITurnkeyContext TurnkeyContext, DeviceInfo Device)
	{
		if (Device == null)
		{
			if (ExitCode == 0)
			{
				if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					TurnkeyContext.PauseForUser("If you had Xcode in your Dock, you will need to remove it and add the new one (even though it was in the same location). macOS follows the move to the Trash for the Dock icon");
				}
			}
		}

		return ExitCode == 0;
	}

	#endregion


	public override void PostStagingFileCopy(ProjectParams Params, DeploymentContext SC)
	{
		ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, Params.RawProjectPath.Directory!, SC.StageTargetPlatform.PlatformType);
		bool bUseModernXcode;
		Ini.TryGetValue("XcodeConfiguration", "bUseModernXcode", out bUseModernXcode);

		if (bUseModernXcode)
		{
			// modern does all the distribution via .xcarchive
			bool bForDistribution = false;

			foreach (TargetReceipt Target in SC.StageTargets.Select(x => x.Receipt))
			{
				Console.WriteLine($"GENERATING STUB PROJECT");
				DirectoryReference GeneratedProjectFile;
				IOSExports.GenerateRunOnlyXcodeProject(SC.RawProjectPath, Target.Platform, Target.TargetName, bForDistribution, Logger, out GeneratedProjectFile);


				Console.WriteLine("FINALIZEING APP with {0}, target {1}", GeneratedProjectFile, Target.TargetName);
				string ExtraOptions = $"SYMROOT=\"{SC.StageDirectory.ParentDirectory}\" EFFECTIVE_PLATFORM_NAME={SC.StageDirectory.GetDirectoryName()}";
				// run xcodebuild on the generated project to make the .app
				IOSExports.FinalizeAppWithModernXcode(GeneratedProjectFile!, Target.Platform, Target.TargetName, Target.Configuration.ToString(), ExtraOptions, bForDistribution, Logger);
				Console.WriteLine("DOME FINALIZEING APP with {0}, target {1}", GeneratedProjectFile, Target.TargetName);
			}
		}
	}
}
