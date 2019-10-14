// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DatasmithRevit2020Target : DatasmithRevitBaseTarget
{
	public DatasmithRevit2020Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithRevit2020";
		ExeBinariesSubFolder = Path.Combine("Revit", "2020");

		AddPostBuildSteps(LaunchModuleName, "Revit_2020_API");
	}
}
