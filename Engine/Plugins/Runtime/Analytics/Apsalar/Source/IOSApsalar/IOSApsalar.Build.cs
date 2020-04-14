// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class IOSApsalar : ModuleRules
	{
		public IOSApsalar( ReadOnlyTargetRules Target ) : base(Target)
		{
			BinariesSubFolder = "NotForLicensees";

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					// ... add other public dependencies that you statically link with here ...
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Analytics",
					// ... add private dependencies that you statically link with here ...
				}
				);

            PublicIncludePathModuleNames.Add("Analytics");

			PublicFrameworks.AddRange(
				new string[] {
					"CoreTelephony",
					"SystemConfiguration",
					"UIKit",
					"Foundation",
					"CoreGraphics",
					"MobileCoreServices",
					"StoreKit",
					"CFNetwork",
					"CoreData",
					"Security",
					"CoreLocation"
				});

			PublicSystemLibraries.AddRange(
				new string[] {
					"sqlite3",
					"z"
				});

			string SDKPath = Path.Combine(EngineDirectory, "Restricted/NotForLicensees/Source/ThirdParty/Apsalar/IOS");

			bool bHasApsalarSDK = Directory.Exists(SDKPath);
            if (bHasApsalarSDK)
            {
                PublicIncludePaths.Add(SDKPath);
                PublicAdditionalLibraries.Add(Path.Combine(SDKPath, "libApsalar.a"));

                PublicDefinitions.Add("WITH_APSALAR=1");
            }
            else
            {
                PublicDefinitions.Add("WITH_APSALAR=0");
            }
        }
	}
}
