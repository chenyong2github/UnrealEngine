// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
public class LowLevelTestsRunner: ModuleRules
{
	public LowLevelTestsRunner(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		PrecompileForTargets = PrecompileTargetsType.None;
		bUseUnity = false;
		bRequiresImplementModule = false;

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Public"),
				Path.Combine(Target.UEThirdPartySourceDirectory, "Catch2")
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(ModuleDirectory, "Private")
			}
		);

		PublicIncludePathModuleNames.AddRange(
				new string[] {
					"Core"
				}
		);
	}
}