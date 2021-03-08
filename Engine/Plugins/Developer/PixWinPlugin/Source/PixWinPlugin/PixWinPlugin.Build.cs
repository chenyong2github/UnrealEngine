// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	using System.IO;

	public class PixWinPlugin : ModuleRules
	{
		public PixWinPlugin(ReadOnlyTargetRules Target) : base(Target)
        {
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"Core",
				"InputCore",
				"RenderCore",
				"InputDevice",
				"RHI",
			});

			PublicSystemLibraries.Add("dxgi.lib");
		}
	}
}
