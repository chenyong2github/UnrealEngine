// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class AESGCMHandlerComponent : ModuleRules
{
	protected virtual bool DefaultToSSL { get { return true; } }

	public AESGCMHandlerComponent(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"PacketHandler",
				"PlatformCrypto",
			}
			);

		PublicIncludePathModuleNames.AddRange(
			new string[]
			{
				"PlatformCrypto"
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
		else if (DefaultToSSL)
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