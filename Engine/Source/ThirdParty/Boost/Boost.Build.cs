// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Boost : ModuleRules
{
	public Boost(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string BoostVersion = "1_70_0";
		string[] BoostLibraries = { "atomic", "chrono", "iostreams", "program_options", "python39", "regex", "system", "thread" };

		string BoostVersionDir = "boost-" + BoostVersion;
		string BoostPath = Path.Combine(Target.UEThirdPartySourceDirectory, "Boost", BoostVersionDir);
		string BoostIncludePath = Path.Combine(BoostPath, "include");
		PublicSystemIncludePaths.Add(BoostIncludePath);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string BoostToolsetVersion = "vc142";

			string BoostLibPath = Path.Combine(BoostPath, "lib", "Win64");
			string BoostVersionShort = BoostVersion.Substring(BoostVersion.Length - 2) == "_0" ? BoostVersion.Substring(0, BoostVersion.Length - 2) : BoostVersion;

			foreach (string BoostLib in BoostLibraries)
			{
				string BoostLibName = "boost_" + BoostLib + "-" + BoostToolsetVersion + "-mt-x64" + "-" + BoostVersionShort;
				PublicAdditionalLibraries.Add(Path.Combine(BoostLibPath, BoostLibName + ".lib"));
				RuntimeDependencies.Add("$(TargetOutputDir)/" + BoostLibName + ".dll", Path.Combine(BoostLibPath, BoostLibName + ".dll"));
			}

			PublicDefinitions.Add("BOOST_LIB_TOOLSET=\"" + BoostToolsetVersion + "\"");
			PublicDefinitions.Add("BOOST_ALL_NO_LIB");
		}
	}
}
