// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class DatasmithRhino7Target : DatasmithRhinoBaseTarget
{
	public DatasmithRhino7Target(TargetInfo Target)
		: base(Target)
	{
	}

	public override string GetVersion() { return "7"; }

	public override string GetRhinoInstallFolder()
	{
		try
		{
			return Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\McNeel\Rhinoceros\7.0\Install", "Path", "") as string;
		}
		catch(Exception)
		{
			return "";
		}
	}
}
