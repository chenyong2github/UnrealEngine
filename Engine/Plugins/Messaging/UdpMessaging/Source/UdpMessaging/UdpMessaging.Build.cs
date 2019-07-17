// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UdpMessaging : ModuleRules
	{
		public UdpMessaging(ReadOnlyTargetRules Target) : base(Target)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Messaging",
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
					"Json",
					"Cbor",
					"Networking",
					"Serialization",
					"Sockets",
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Messaging",
					"MessagingCommon",
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					"UdpMessaging/Private",
					"UdpMessaging/Private/Shared",
					"UdpMessaging/Private/Transport",
					"UdpMessaging/Private/Tunnel",
				});

			if (Target.Type == TargetType.Editor)
			{
				DynamicallyLoadedModuleNames.AddRange(
					new string[] {
						"Settings",
					});

				PrivateIncludePathModuleNames.AddRange(
					new string[] {
						"Settings",
					});
			}

			//temporary workaround to allow automatic configuration of static endpoints in editor/UFE for certain target devices, pending proper API
			if (Target.Type == TargetType.Editor || (Target.Type == TargetType.Program && Target.Name == "UnrealFrontend"))
			{
				PublicDefinitions.Add("WITH_TARGETPLATFORM_SUPPORT=1");

				DynamicallyLoadedModuleNames.AddRange(
					new string[] {
						"TargetPlatform",
					});

				PrivateIncludePathModuleNames.AddRange(
					new string[] {
						"TargetPlatform",
					});
			}
			else
			{
				PublicDefinitions.Add("WITH_TARGETPLATFORM_SUPPORT=0");
			}

		}
	}
}
