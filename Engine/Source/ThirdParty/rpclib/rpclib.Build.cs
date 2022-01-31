// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

[SupportedPlatforms("Win64", "Mac", "Linux")]
public class RPCLib : ModuleRules
{
	public RPCLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!IsVcPackageSupported)
		{
			return;
		}

		AddVcPackage("rpclib", true, "rpc");
	}
}
