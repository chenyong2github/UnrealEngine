// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ExrReaderGpu : ModuleRules
	{
		public ExrReaderGpu(ReadOnlyTargetRules Target) : base(Target)
		{
			if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
            {
				return;
            }

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

			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "UEOpenExr");
				PrivateDependencyModuleNames.Add("OpenExrWrapper");
				PrivateIncludePaths.Add("ExrReaderGpu/Private");
			}

			if (Target.Type == TargetType.Editor)
			{
				PublicDependencyModuleNames.Add("UnrealEd");
				PrivateDependencyModuleNames.Add("UnrealEd");
			}

        }
	}
}
