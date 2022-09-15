// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class NNXCore : ModuleRules
    {
        public NNXCore(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
				}
			);

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"Core",
					"CoreUObject",
					"Engine",
				}
			);
        }
    }
}
