// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright 2020 Nicholas Frechette. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ACLPluginEditor : ModuleRules
	{
		public ACLPluginEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			CppStandard = CppStandardVersion.Cpp17;

			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			//OptimizeCode = CodeOptimization.Never;

			PublicDependencyModuleNames.Add("ACLPlugin");
			PublicDependencyModuleNames.Add("Core");
			PublicDependencyModuleNames.Add("CoreUObject");
			PublicDependencyModuleNames.Add("Engine");

			PrivateDependencyModuleNames.Add("Slate");
			PrivateDependencyModuleNames.Add("SlateCore");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
