// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MLAdapterInference : ModuleRules
	{
		public MLAdapterInference(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PrivateIncludePaths.Add("MLAdapterInference/Private");

			PublicIncludePaths.AddRange(
				new string[] {
							"Runtime/AIModule/Public",
							ModuleDirectory + "/Public",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"MLAdapter",
					"NeuralNetworkInference",
				}
			);

			PrivateDependencyModuleNames.AddRange(
                new string[] {
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
				}
			);

			// RPCLib disabled on other platforms at the moment
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicDefinitions.Add("WITH_RPCLIB=1");
				AddEngineThirdPartyPrivateStaticDependencies(Target, "RPCLib");

				string RPClibDir = Path.Combine(Target.UEThirdPartySourceDirectory, "rpclib");
				PublicIncludePaths.Add(Path.Combine(RPClibDir, "Source", "include"));
			}
			else
			{
				PublicDefinitions.Add("WITH_RPCLIB=0");
			}

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("EditorFramework");
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}
	}
}
