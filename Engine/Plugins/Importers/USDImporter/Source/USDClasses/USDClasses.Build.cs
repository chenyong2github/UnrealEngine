// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDClasses : ModuleRules
	{
		public USDClasses(ReadOnlyTargetRules Target) : base(Target)
        {
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Analytics",
					"Core",
					"CoreUObject",
					"Engine",
					"Slate",
					"SlateCore"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"DeveloperSettings",
					"InputCore",
					"Json", // To read/write plugInfo.json files from UnrealUSDWrapper.cpp
				}
			);

			if (Target.Configuration != UnrealTargetConfiguration.Shipping)
			{
				PublicDefinitions.Add("UE_TRACE_ENABLED=1");
			}
		}
	}
}
