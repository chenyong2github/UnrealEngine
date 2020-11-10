// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ExrReaderGpu : ModuleRules
	{
		public ExrReaderGpu(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableExceptions = true;
			bUseRTTI = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"TimeManagement",
					"RHI",
					"RenderCore",
					"Renderer",
					"Slate",
					"SlateCore",
					"Projects",
					"Engine",
				});

			if (Target.Type == TargetType.Editor)
			{
				PublicDependencyModuleNames.Add("UnrealEd");
				PrivateDependencyModuleNames.Add("UnrealEd");
			}

			if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "UEOpenExr");
				PrivateDependencyModuleNames.Add("OpenExrWrapper");
				PrivateIncludePaths.Add("ExrReaderGpu/Private");
			}

		}
	}
}
