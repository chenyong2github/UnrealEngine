// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Core_HoloLens : Core
	{
		public Core_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			// This module needs to be referenced from a TargetPlatform module, so don't leak our platform info out
			if (Target.Platform == UnrealTargetPlatform.HoloLens)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target,
					"zlib");

				AddEngineThirdPartyPrivateStaticDependencies(Target,
					"IntelTBB",
					"XInput"
					);

				PublicDefinitions.Add("WITH_VS_PERF_PROFILER=0");
				PublicDefinitions.Add("IS_RUNNING_GAMETHREAD_ON_EXTERNAL_THREAD=1");
			}
		}
	}
}
