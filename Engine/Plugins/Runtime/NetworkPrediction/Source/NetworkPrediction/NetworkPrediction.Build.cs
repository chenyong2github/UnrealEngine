// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NetworkPrediction : ModuleRules
	{
		public NetworkPrediction(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.AddRange(
				new string[] {
                    ModuleDirectory + "/Public",
                    "Runtime/TraceLog/Public",
                }
				);

			PrivateIncludePaths.AddRange(
				new string[] {
                    "NetworkPrediction/Private",
				}
				);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "RenderCore",
					"PhysicsCore",
					"Chaos",
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"TraceLog"					
                }
				);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
				}
				);



            // Only needed for the PIE delegate in FNetworkPredictionModule::StartupModule
            if (Target.Type == TargetType.Editor) {
                PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "UnrealEd",
                });
            }

        }
	}
}
