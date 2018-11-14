// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class OnlineSubsystemGoogle : ModuleRules
{
	public OnlineSubsystemGoogle(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bUsesRestfulImpl = false;
		PrivateDefinitions.Add("ONLINESUBSYSTEMGOOGLE_PACKAGE=1");
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"HTTP",
				"ImageCore",
				"Json",
				"Sockets",
				"OnlineSubsystem", 
			}
			);

		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicDefinitions.Add("WITH_GOOGLE=1");
			PublicDefinitions.Add("UE4_GOOGLE_VER=4.0.1");
		   	PrivateIncludePaths.Add("Private/IOS");

			// These are iOS system libraries that Google depends on
			PublicFrameworks.AddRange(
			new string[] {
				"SafariServices",
				"SystemConfiguration"
			});

			PublicAdditionalFrameworks.Add(
			new UEBuildFramework(
				"GoogleSignIn",
				"ThirdParty/IOS/GoogleSignInSDK/GoogleSignIn.embeddedframework.zip",
				"GoogleSignIn.bundle"
			)
			);

			PublicAdditionalFrameworks.Add(
			new UEBuildFramework(
				"GoogleSignInDependencies",
				"ThirdParty/IOS/GoogleSignInSDK/GoogleSignInDependencies.embeddedframework.zip"
			)
			);
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Launch",
			}
			);

			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "OnlineSubsystemGoogle_UPL.xml"));

			PrivateIncludePaths.Add("Private/Android");
			
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			bUsesRestfulImpl = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			PrivateIncludePaths.Add("Private/XboxOne");
		}
		else if (Target.Platform == UnrealTargetPlatform.PS4)
		{
			PrivateIncludePaths.Add("Private/PS4");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			bUsesRestfulImpl = true;
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			bUsesRestfulImpl = true;
		}
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            bUsesRestfulImpl = true;
        }
		else
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		if (bUsesRestfulImpl)
		{
			PublicDefinitions.Add("USES_RESTFUL_GOOGLE=1");
			PrivateIncludePaths.Add("Private/Rest");
		}
		else
		{
			PublicDefinitions.Add("USES_RESTFUL_GOOGLE=0");
		}
	}
}
