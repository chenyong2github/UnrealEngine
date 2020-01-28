// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class WebAuth : ModuleRules
{
	public WebAuth(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			// << Add these lines to your project's build.cs when building for iOS 12+
			// PublicFrameworks.Add("AuthenticationServices");
			// PublicDefinitions.Add("WEBAUTH_PLATFORM_IOS_12");

			// iOS 11+ legacy support
			PublicFrameworks.Add("SafariServices");
		}

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.Add("Launch");

			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "WebAuth_UPL.xml"));
		}
	}
}
