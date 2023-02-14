// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ReplicationSystemTestPlugin : ModuleRules
	{
		public ReplicationSystemTestPlugin(ReadOnlyTargetRules Target) : base(Target)
		{
			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

			// We never want to precompile this plugin
			PrecompileForTargets = PrecompileTargetsType.None;

			PublicIncludePaths.AddRange(
				new string[] {
					ModuleDirectory + "/Public",

				}
				);

			PrivateIncludePaths.AddRange(
				new string[]
				{
                    Path.Combine(EngineDir, "Source/Runtime/Experimental/Iris/Core/Private"),
				}
				);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Projects",
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"NetCore",
				}
				);

			if (Target.IsTestTarget)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"LowLevelTestsRunner",
					}
					);
			}

			PrivateDefinitions.Add(String.Format("UE_NET_WITH_LOW_LEVEL_TESTS={0}", Target.ExplicitTestsTarget ? "1" : "0"));

			SetupIrisSupport(Target);
		}
	}
}
