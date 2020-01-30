// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Expat : ModuleRules
{
	protected virtual string ExpatVersion			{ get { return "expat-2.2.0"; } }

	protected virtual string IncRootDirectory		{ get { return ModuleDirectory; } }
	protected virtual string LibRootDirectory		{ get { return ModuleDirectory; } }

	protected virtual string ExpatPackagePath		{ get { return Path.Combine(LibRootDirectory, ExpatVersion); } }
	protected virtual string ExpatIncludePath		{ get { return Path.Combine(IncRootDirectory, ExpatVersion, "lib"); } }

	protected virtual string ConfigName				{ get {	return (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? "Debug" : "Release"; } }

	public Expat(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(ExpatIncludePath);

		if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			string ToolchainName = "VS";
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				ToolchainName += VersionName.ToString();
			}

			string LibraryPath = Path.Combine(ExpatPackagePath, "XboxOne", ToolchainName, ConfigName);
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "expat.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string LibraryPath = Path.Combine(ExpatPackagePath, "Android", ConfigName);
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "arm64", "libexpat.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "armv7", "libexpat.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ExpatPackagePath, "IOS", ConfigName, "libexpat.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				string LibraryPath = Path.Combine(ExpatPackagePath, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), "Debug");
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "expatd.lib"));
			}
			else
			{
				string LibraryPath = Path.Combine(ExpatPackagePath, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), "Release");
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "expat.lib"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4 || Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Switch)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ExpatPackagePath, Target.Platform.ToString(), ConfigName, "libexpat.a"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(ExpatPackagePath, "Linux/" + Target.Architecture, ConfigName, "libexpat.a"));
		}
	}
}
