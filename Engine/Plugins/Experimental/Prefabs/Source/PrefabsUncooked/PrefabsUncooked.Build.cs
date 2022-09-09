// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PrefabsUncooked : ModuleRules
	{
		public PrefabsUncooked(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine"
                }
			); 
		}
	}
}
