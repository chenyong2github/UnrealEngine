// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DumpGPUServices : ModuleRules
	{
		public DumpGPUServices(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.AddRange(
				new string[] {
					EngineDirectory + "/Source/Runtime/RenderCore/Private",
				}
				);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"RenderCore",
					"Json",
					"HTTP",
				}
				);

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
					"Slate",
					}
					);
			}
		}
	}
}
