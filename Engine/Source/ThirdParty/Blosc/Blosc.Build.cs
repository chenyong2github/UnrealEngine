// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class Blosc : ModuleRules
{
	public Blosc(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string BloscPath = Target.UEThirdPartySourceDirectory + "Blosc/c-blosc-1.5.0/";

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string BloscWin64Path = Path.Combine(BloscPath, "Win64", "VS2019");
			PublicSystemIncludePaths.Add(Path.Combine(BloscWin64Path, "include"));
			PublicAdditionalLibraries.Add(Path.Combine(BloscWin64Path, "lib", "libblosc.lib"));
		}
		else
        {
			string Err = "Platform " + Target.Platform.ToString() + " not supported!";
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}
	}
}
