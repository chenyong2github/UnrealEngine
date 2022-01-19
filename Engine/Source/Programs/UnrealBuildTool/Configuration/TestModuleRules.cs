// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System.IO;

namespace UnrealBuildTool
{
	/// <summary>
	/// ModuleRules extension for low level tests.
	/// </summary>
	public class TestModuleRules : ModuleRules
	{
		/// <summary>
		/// Associated tested module of this test module.
		/// </summary>
		public ModuleRules TestedModule { get; private set; }

		/// <summary>
		/// Constructs a TestModuleRules object with an associated tested module.
		/// </summary>
		public TestModuleRules(ModuleRules TestedModule) : base(new ReadOnlyTargetRules(TestedModule.Target.TestTarget))
		{
			this.TestedModule = TestedModule;

			Name = TestedModule.Name + "Tests";
			if (!string.IsNullOrEmpty(TestedModule.ShortName))
			{
				ShortName = TestedModule.ShortName + "Tests";
			}

			File = FileReference.Combine(UnrealBuildTool.EngineSourceDirectory, "UnrealBuildTool", "Configuration", "TestModuleRules.cs");
			Directory = DirectoryReference.Combine(TestedModule.Directory, "Tests");

			Context = TestedModule.Context;

			PCHUsage = PCHUsageMode.NoPCHs;
			PrecompileForTargets = PrecompileTargetsType.None;

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.Platform == UnrealTargetPlatform.Linux)
			{
				OptimizeCode = CodeOptimization.Never;
			}

			bAllowConfidentialPlatformDefines = true;
			bLegalToDistributeObjectCode = true;

			// Required false for catch.hpp
			bUseUnity = false;

			// Disable exception handling so that tests can assert for exceptions
			bEnableObjCExceptions = false;
			bEnableExceptions = false;

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"Projects",
					"LowLevelTestsRunner"
				});

			PrivateIncludePaths.Add("Runtime/Launch/Private");
			PrivateIncludePathModuleNames.Add("Launch");

			// Tests can refer to tested module's Public and Private paths
			PublicIncludePaths.Add(Path.Combine(TestedModule.ModuleDirectory, "Public"));
			PrivateIncludePaths.Add(Path.Combine(TestedModule.ModuleDirectory, "Private"));

			// Platforms specific setup
			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				PublicDefinitions.Add("CATCH_CONFIG_NOSTDOUT");
			}
		}
	}
}
