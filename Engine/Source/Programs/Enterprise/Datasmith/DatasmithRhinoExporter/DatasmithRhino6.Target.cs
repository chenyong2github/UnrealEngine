// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DatasmithRhino6Target : DatasmithRhinoBaseTarget
{
	public DatasmithRhino6Target(TargetInfo Target)
		: base(Target)
	{
	}

	public override string GetVersion() { return "6"; }
}
