// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class VivoxVoiceChat : ModuleRules
	{
		public VivoxVoiceChat(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"ApplicationCore"
				}
			);

			PublicIncludePathModuleNames.AddRange(
				new string[]
				{
					"VoiceChat"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"VivoxCoreSDK",
					"VivoxClientAPI"
				}
			);

			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"ApplicationCore",
						"Launch"
					}
				);
			}

			PrivateDefinitions.Add("VIVOXVOICECHAT_PACKAGE");
		}
	}
}
