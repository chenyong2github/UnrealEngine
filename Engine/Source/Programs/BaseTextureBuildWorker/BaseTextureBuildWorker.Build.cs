// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using EpicGames.Core;
using UnrealBuildTool;

public class BaseTextureBuildWorker : ModuleRules
{
	public BaseTextureBuildWorker(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"DerivedDataCache",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"DerivedDataBuildWorker",
			"TextureBuild",
			"TextureFormat",
			"TextureFormatUncompressed",
		});

		// Determine if TextureFormatOodle is enabled.
		var TextureFormatOodleUPluginFile = FileReference.Combine(new DirectoryReference(EngineDirectory), "Plugins/Developer/TextureFormatOodle/TextureFormatOodle.uplugin");
		var TextureFormatOodlePlugin = new PluginInfo(TextureFormatOodleUPluginFile, PluginType.Engine);

		bool bTextureFormatOodlePluginEnabled =
		Enum.GetValues(typeof(UnrealTargetConfiguration)).Cast<UnrealTargetConfiguration>().Any(config
				=> Plugins.IsPluginEnabledForTarget(TextureFormatOodlePlugin, null, Target.Platform, config, TargetType.Program));

		if (bTextureFormatOodlePluginEnabled)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"TextureFormatOodle",
			});
		}

	}
}
