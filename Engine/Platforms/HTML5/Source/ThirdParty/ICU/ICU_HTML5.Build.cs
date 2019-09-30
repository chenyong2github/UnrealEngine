// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ICU_HTML5 : ICU
{
	public ICU_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		string ICULibPath = Path.Combine(Target.HTML5Platform.PlatformThirdPartySourceDirectory, "ICU/icu4c-53_1");

		string[] LibraryNameStems =
		{
			"data", // Data
			"uc",   // Unicode Common
			"i18n", // Internationalization
			"le",   // Layout Engine
			"lx",   // Layout Extensions
			"io"    // Input/Output
		};
//		string LibraryNamePostfix = (Target.Configuration == UnrealTargetConfiguration.Debug) ? "d" : string.Empty;

		foreach (string Stem in LibraryNameStems)
		{
			PublicAdditionalLibraries.Add(ICULibPath + "/libicu" + Stem + Target.HTML5OptimizationSuffix + ".bc");
		}
	}
}
