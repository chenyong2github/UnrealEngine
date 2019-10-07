// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DatasmithRevit2019Target : DatasmithRevitBaseTarget
{
	public DatasmithRevit2019Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithRevit2019";
		ExeBinariesSubFolder = Path.Combine("Revit", "2019");
		
		AddPostBuildSteps(LaunchModuleName, "Revit_2019_API");
	}
}
