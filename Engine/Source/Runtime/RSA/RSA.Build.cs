// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RSA : ModuleRules
{
	public RSA(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");

        if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Linux)				
		{
			PrivateDependencyModuleNames.Add("OpenSSL");
			PrivateDefinitions.Add("RSA_USE_OPENSSL=1");
		}
		else
		{
			PrivateDefinitions.Add("RSA_USE_OPENSSL=0");
		}
	}
}
