// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class Blosc : ModuleRules
{
	public Blosc(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		const string bloscVersion = "1.5.0";

		string BloscDir = Target.UEThirdPartySourceDirectory + "Blosc/Deploy/c-blosc-" + bloscVersion;
		string BloscBinDir = Path.Combine(BloscDir, "bin");
		string BloscIncludeDir = Path.Combine(BloscDir, "include");
		string BloscLibDir = Path.Combine(BloscDir, "lib");
		
		PublicIncludePaths.Add(BloscIncludeDir);
		
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string Win64Config = "Release";
			string Win64Dir = Path.Combine("Win64", "VS2019", Win64Config);
			
			const string bloscWin64LibName = "libblosc.lib";
			string BloscWin64LibDir = Path.Combine(BloscLibDir, Win64Dir);
			string BloscWin64Lib = Path.Combine(BloscWin64LibDir, bloscWin64LibName);

			PublicAdditionalLibraries.Add(BloscWin64Lib);
		}
		else
        {
	        string Err = "Platform " + Target.Platform + " not supported!";
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}
	}
}
