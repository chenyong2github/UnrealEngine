// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GooglePAD : ModuleRules
{
	public GooglePAD(ReadOnlyTargetRules Target) : base(Target)
	{
		string NDKVersion = "ndk21.4.7075529";
		
		PublicIncludePaths.AddRange(
			new string[] {
				"Runtime/Launch/Public"
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"GooglePAD/Private",
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "GooglePAD_APL.xml"));

			string PlayCoreSDKPath = Path.Combine(ModuleDirectory, "..", "ThirdParty", "play-core-native-sdk");
			string PlayCoreIncludePath = Path.Combine(PlayCoreSDKPath, "include");
			string PlayCoreLibPath = Path.Combine(PlayCoreSDKPath, "libs");

			PublicAdditionalLibraries.Add(Path.Combine(PlayCoreLibPath, "armeabi-v7a", NDKVersion, "c++_shared", "libplaycore.so"));
			PublicAdditionalLibraries.Add(Path.Combine(PlayCoreLibPath, "arm64-v8a", NDKVersion, "c++_shared", "libplaycore.so"));
			PublicAdditionalLibraries.Add(Path.Combine(PlayCoreLibPath, "x86", NDKVersion, "c++_shared", "libplaycore.so"));
			PublicAdditionalLibraries.Add(Path.Combine(PlayCoreLibPath, "x86_64", NDKVersion, "c++_shared", "libplaycore.so"));

			PrivateIncludePaths.Add(PlayCoreIncludePath);
		}
	}
}
