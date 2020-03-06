// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DatasmithSketchUp2020Target : DatasmithSketchUpBaseTarget
{
	public DatasmithSketchUp2020Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithSketchUp2020";
		ExeBinariesSubFolder = Path.Combine("SketchUp", "2020");
		
		AddCopyPostBuildStep(Target, "skp2udatasmith2020");
	}
}
