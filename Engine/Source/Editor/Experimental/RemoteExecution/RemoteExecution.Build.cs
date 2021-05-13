// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class RemoteExecution : ModuleRules
	{
		public RemoteExecution(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"Settings",
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
				}
			);
		}
	}
}
