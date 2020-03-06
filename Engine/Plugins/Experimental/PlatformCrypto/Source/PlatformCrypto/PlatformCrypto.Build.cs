// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PlatformCrypto : ModuleRules
	{
		protected virtual bool DefaultToSSL { get { return true; } }

		public PlatformCrypto(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"PlatformCryptoTypes",
				}
				);

			if (DefaultToSSL)
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
