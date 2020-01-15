// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceAnalysis : ModuleRules
{
	public TraceAnalysis(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Cbor");
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("DirectoryWatcher");
		PrivateDependencyModuleNames.Add("Sockets");
		PrivateDependencyModuleNames.Add("TraceLog");

		PrivateDefinitions.Add("ASIO_SEPARATE_COMPILATION");
		PrivateDefinitions.Add("ASIO_STANDALONE");
		PrivateDefinitions.Add("ASIO_NO_EXCEPTIONS");
		PrivateDefinitions.Add("ASIO_NO_TYPEID");
		PrivateIncludePaths.Add("Developer/TraceAnalysis/Private/Asio");
	}
}
