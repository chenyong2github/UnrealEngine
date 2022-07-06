// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ADOSupport: ModuleRules
	{
		public ADOSupport(ReadOnlyTargetRules Target) : base(Target)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "ADO");

			if (Target.Platform == UnrealTargetPlatform.Win64 &&
				Target.WindowsPlatform.Compiler != WindowsCompiler.Clang &&
				Target.StaticAnalyzer != StaticAnalyzer.PVSStudio)
			{
				string MsAdo15 = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonProgramFiles), "System", "ADO", "msado15.dll");
				TypeLibraries.Add(new TypeLibrary(MsAdo15, "rename(\"EOF\", \"ADOEOF\")", "msado15.tlh"));
			}
			
			PublicIncludePaths.AddRange(
				new string[] {
					// ... add public include paths required here ...
				}
				);

			PrivateIncludePaths.AddRange(
				new string[] {
					// ... add other private include paths required here ...
				}
				);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"DatabaseSupport",
					// ... add other public dependencies that you statically link with here ...
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					// ... add private dependencies that you statically link with here ...
				}
				);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
				}
				);
		}
	}
}
