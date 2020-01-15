// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DatasmithSketchUp2018Target : DatasmithSketchUpBaseTarget
{
	public DatasmithSketchUp2018Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithSketchUp2018";
		ExeBinariesSubFolder = Path.Combine("SketchUp", "2018");

		AddCopyPostBuildStep(Target, "skp2udatasmith2018");
	}
}
