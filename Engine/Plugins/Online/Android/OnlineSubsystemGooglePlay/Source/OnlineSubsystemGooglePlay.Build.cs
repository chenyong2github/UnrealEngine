// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using Tools.DotNETCommon;
using UnrealBuildTool;

public class OnlineSubsystemGooglePlay : ModuleRules
{
	[ConfigFile(ConfigHierarchyType.Engine, "OnlineSubsystemGooglePlay.Store")]
	bool bUseGooglePlayBillingApiV2 = true;

	public OnlineSubsystemGooglePlay(ReadOnlyTargetRules Target) : base(Target)
    {
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		ConfigCache.ReadSettings(DirectoryReference.FromFile(Target.ProjectFile), Target.Platform, this);

		PublicDefinitions.Add("ONLINESUBSYSTEMGOOGLEPLAY_PACKAGE=1");
		PublicDefinitions.Add("OSSGOOGLEPLAY_WITH_AIDL=" + (bUseGooglePlayBillingApiV2 ? "0" : "1"));

		PrivateIncludePaths.AddRange(
			new string[] {
				"Private",    
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[] { 
                "Core", 
                "CoreUObject", 
                "Engine", 
                "Sockets",
				"OnlineSubsystem", 
                "Http",
				"AndroidRuntimeSettings",
				"Launch",
				"GpgCppSDK"
            }
			);

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"AndroidPermission"
			}
			);

        string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
        AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "OnlineSubsystemGooglePlay_UPL.xml"));
    }
}
