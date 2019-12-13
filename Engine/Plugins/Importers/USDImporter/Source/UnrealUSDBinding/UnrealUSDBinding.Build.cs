// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class UnrealUSDBinding : ModuleRules
	{
		public UnrealUSDBinding(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
                    "UnrealUSDWrapper"
                }
			);
		}
	}
}
