// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DirectX : ModuleRules
{
	public static string GetDir(ReadOnlyTargetRules Target)
	{
		return Target.UEThirdPartySourceDirectory + "Windows/DirectX";
	}

	public static string GetIncludeDir(ReadOnlyTargetRules Target)
	{
		return GetDir(Target) + "/include";
	}

	public static string GetLibDir(ReadOnlyTargetRules Target)
	{
		string DirectXSDKDir = GetDir(Target);
		if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
		{
			return DirectXSDKDir + "/Lib/arm64/";
		}
		return DirectXSDKDir + "/Lib/x64/";
	}

	public static string GetDllDir(ReadOnlyTargetRules Target)
	{
		string DirectXSDKDir = Path.Combine(Target.RelativeEnginePath, "Binaries/ThirdParty/Windows/DirectX");
		if (Target.WindowsPlatform.Architecture == WindowsArchitecture.ARM64)
		{
			return DirectXSDKDir + "/arm64/";
		}
		return DirectXSDKDir + "/x64/";
	}

	public DirectX(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
	}
}

