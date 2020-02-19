// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapARPinImpl : ModuleRules
	{
		public MagicLeapARPinImpl( ReadOnlyTargetRules Target ) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"MagicLeapARPin"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HeadMountedDisplay",
					"MLSDK",
					"MagicLeap"
				}
			);
		}
	}
}
