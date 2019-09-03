// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PlatformCrypto : ModuleRules
	{
		public PlatformCrypto(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"PlatformCryptoTypes",
				}
				);

			if (Target.Platform == UnrealTargetPlatform.XboxOne)
			{
				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"PlatformCryptoBCrypt",
					}
					);
			}
			else if (Target.Platform == UnrealTargetPlatform.Switch)
			{
				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"PlatformCryptoSwitch",
					}
					);
			}
			else
			{
				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"PlatformCryptoOpenSSL",
					}
					);
			}
		}
	}
}
