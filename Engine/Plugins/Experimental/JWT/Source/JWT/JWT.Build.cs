// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class JWT : ModuleRules
	{
		public JWT(ReadOnlyTargetRules Target) : base(Target)
		{
			OptimizeCode = CodeOptimization.InShippingBuildsOnly;

			PublicIncludePaths.AddRange(
				new string[] {
					// ... add public include paths required here ...
					Path.Combine(EngineDirectory, "Plugins/Experimental/PlatformCrypto/Source/PlatformCrypto/Public/"),
					Path.Combine(EngineDirectory, "Plugins/Experimental/PlatformCrypto/Source/PlatformCryptoOpenSSL/Public/"),
					Path.Combine(EngineDirectory, "Plugins/Experimental/PlatformCrypto/Source/PlatformCryptoTypes/Public/"),
				}
			);

			PrivateIncludePaths.AddRange(
				new string[] {
					// ... add other private include paths required here ...
					Path.Combine(EngineDirectory, "Plugins/Experimental/PlatformCrypto/Source/PlatformCrypto/Private/"),
					Path.Combine(EngineDirectory, "Plugins/Experimental/PlatformCrypto/Source/PlatformCryptoOpenSSL/Private/"),
					Path.Combine(EngineDirectory, "Plugins/Experimental/PlatformCrypto/Source/PlatformCryptoTypes/Private/"),
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					// ... add other public dependencies that you statically link with here ...
					"Json",
					"PlatformCrypto",
					"PlatformCryptoOpenSSL",
					"PlatformCryptoTypes",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					// ... add private dependencies that you statically link with here ...
					"PlatformCrypto",
					"PlatformCryptoOpenSSL",
					"PlatformCryptoTypes",
				}
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
				}
			);
		}
	}
}
