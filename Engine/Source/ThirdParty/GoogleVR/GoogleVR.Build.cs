// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GoogleVR : ModuleRules
{
	public GoogleVR(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string GoogleVRSDKDir = Target.UEThirdPartySourceDirectory + "GoogleVR/";
		PublicSystemIncludePaths.AddRange(
			new string[] {
					GoogleVRSDKDir + "include",
					GoogleVRSDKDir + "include/vr/gvr/capi/include",
				}
			);

		string GoogleVRBaseLibPath = GoogleVRSDKDir + "lib/";

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(GoogleVRBaseLibPath+"mac/libgvr.a");
			PublicAdditionalLibraries.Add(GoogleVRBaseLibPath+"mac/libgvraux.a");
		}

		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicAdditionalLibraries.Add(GoogleVRBaseLibPath+"win32/libgvr.lib");
			PublicAdditionalLibraries.Add(GoogleVRBaseLibPath+"win32/libgvraux.lib");
		}

		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(GoogleVRBaseLibPath+"win64/libgvr.lib");
			PublicAdditionalLibraries.Add(GoogleVRBaseLibPath+"win64/libgvraux.lib");
		}

		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// toolchain will filter properly
			PublicAdditionalLibraries.Add( GoogleVRBaseLibPath + "android/armv7/libgvr.so");
			PublicAdditionalLibraries.Add( GoogleVRBaseLibPath + "android/arm64/libgvr.so");
			PublicAdditionalLibraries.Add( GoogleVRBaseLibPath + "android/x86/libgvr.so");
			PublicAdditionalLibraries.Add( GoogleVRBaseLibPath + "android/x86_64/libgvr.so");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			string GoogleVRIOSLibPath = GoogleVRBaseLibPath + "ios/";

			// Libraries that the GVR SDK depend on.
			PublicAdditionalLibraries.Add(GoogleVRIOSLibPath+"libGTMSessionFetcher.a");

			// Frameworks that GoogleVR frame depends on
			PublicAdditionalFrameworks.Add(new Framework("CoreText"));
			PublicAdditionalFrameworks.Add(new Framework("AudioToolbox"));
			PublicAdditionalFrameworks.Add(new Framework("AVFoundation"));
			PublicAdditionalFrameworks.Add(new Framework("CoreGraphics"));
			PublicAdditionalFrameworks.Add(new Framework("CoreMotion"));
			PublicAdditionalFrameworks.Add(new Framework("CoreVideo"));
			PublicAdditionalFrameworks.Add(new Framework("GLKit"));
			PublicAdditionalFrameworks.Add(new Framework("MediaPlayer"));
			PublicAdditionalFrameworks.Add(new Framework("OpenGLES"));
			PublicAdditionalFrameworks.Add(new Framework("QuartzCore"));

			// GoogleVR framework.
			// Note: Had to add 5 times because there are 5 different resource bundles and there doesn't seem to be support for
			//       just adding resource bundles on iOS
			PublicAdditionalFrameworks.Add(
				new Framework(
					"GVRSDK",														// Framework name
					"lib/ios/ThirdPartyFrameworks/GVRSDK.embeddedframework.zip",			// Zip name
					"GVRSDK.framework/Resources/GoogleKitCore.bundle"				// Resources we need copied and staged
				)
			);
			PublicAdditionalFrameworks.Add(
				new Framework(
					"GVRSDK",														// Framework name
					"lib/ios/ThirdPartyFrameworks/GVRSDK.embeddedframework.zip",			// Zip name
					"GVRSDK.framework/Resources/GoogleKitDialogs.bundle"			// Resources we need copied and staged
				)
			);
			PublicAdditionalFrameworks.Add(
				new Framework(
					"GVRSDK",														// Framework name
					"lib/ios/ThirdPartyFrameworks/GVRSDK.embeddedframework.zip",			// Zip name
					"GVRSDK.framework/Resources/CardboardSDK.bundle"				// Resources we need copied and staged
				)
			);
			PublicAdditionalFrameworks.Add(
				new Framework(
					"GVRSDK",														// Framework name
					"lib/ios/ThirdPartyFrameworks/GVRSDK.embeddedframework.zip",			// Zip name
					"GVRSDK.framework/Resources/GoogleKitHUD.bundle"				// Resources we need copied and staged
				)
			);
			PublicAdditionalFrameworks.Add(
				new Framework(
					"GVRSDK",														// Framework name
					"lib/ios/ThirdPartyFrameworks/GVRSDK.embeddedframework.zip",			// Zip name
					"GVRSDK.framework/Resources/MaterialRobotoFontLoader.bundle"	// Resources we need copied and staged
				)
			);
		}
	}
}
