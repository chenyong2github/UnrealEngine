// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;
using System.Collections.Generic;
using Microsoft.Win32;
using System.Diagnostics;


namespace UnrealBuildTool.Rules
{
	public class AzureSpatialAnchorsForARCore : ModuleRules
	{
		public AzureSpatialAnchorsForARCore(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableExceptions = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AugmentedReality",
					"GoogleARCoreBase"
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AzureSpatialAnchors"
				}
			);

			// Resolve arcore_c_api.h
			PrivateIncludePaths.Add(Path.Combine(Path.GetFullPath(Target.RelativeEnginePath), "Source/ThirdParty/GoogleARCore/include"));
			
			// Pull down ASA NDK
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(ModuleDirectory, "AzureSpatialAnchorsForARCore_APL.xml"));

			string libpath = Path.Combine(ModuleDirectory, "ThirdParty");
			PrivateIncludePaths.Add(Path.Combine(libpath, "Include"));
			PublicAdditionalLibraries.Add(Path.Combine(libpath, "armeabi-v7a", "libazurespatialanchorsndk.so"));
			PublicAdditionalLibraries.Add(Path.Combine(libpath, "arm64-v8a", "libazurespatialanchorsndk.so"));
			PublicAdditionalLibraries.Add(Path.Combine(libpath, "x86", "libazurespatialanchorsndk.so"));
		}
	}
}
