// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DatasmithSketchUp2019Target : DatasmithSketchUpBaseTarget
{
	public DatasmithSketchUp2019Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithSketchUp2019";
		ExeBinariesSubFolder = Path.Combine("SketchUp", "2019");
		
		AddCopyPostBuildStep(Target, "skp2udatasmith2019");
	}
}
