// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealFrontend : ModuleRules
{
	public UnrealFrontend( ReadOnlyTargetRules Target ) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		PrivateIncludePaths.AddRange(
			new string[] {
				"Programs/UnrealFrontend/Private/Commands",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AutomationController",
				"Core",
				"ApplicationCore",
				"CoreUObject",
				"DeviceManager",
				"LauncherServices",
				"Messaging",
                "OutputLog",
				"Profiler",
				"ProfilerClient",
                "ProjectLauncher",
				"Projects",
				"SessionFrontend",
				"SessionServices",                
                "Slate",
				"SlateCore",
				"SourceCodeAccess",
				"StandaloneRenderer",
				"TargetDeviceServices",
				"TargetPlatform",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"SlateReflector",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"SlateReflector"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.Add("XCodeSourceCodeAccess");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("VisualStudioSourceCodeAccess");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnixCommonStartup"
				}
			);
		}

		// @todo: allow for better plug-in support in standalone Slate apps
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Networking",
				"Sockets",
				"UdpMessaging",
                "TcpMessaging",
				"DirectoryWatcher"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Messaging",
			}
		);
	}
}
