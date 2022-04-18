// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class MetalCpp : ModuleRules
{
	public MetalCpp(ReadOnlyTargetRules Target) : base(Target)
	{
		// This module does not require the IMPLEMENT_MODULE macro to be implemented.
		bRequiresImplementModule = false;

		// Where to find the metal-cpp headers.
		PublicIncludePaths.Add("ThirdParty/Apple/metal-cpp/Include");

		// Link in the necessary Apple frameworks.
		PublicFrameworks.AddRange(
			new string[] {
				"Foundation",
				"Metal"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("QuartzCore");
		}
	}
}
