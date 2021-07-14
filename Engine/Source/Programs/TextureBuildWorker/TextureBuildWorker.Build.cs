// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using EpicGames.Core;
using UnrealBuildTool;

// Abstract base class for texture worker targets.  Not a valid target by itself, hence it is not put into a *.target.cs file.
[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public abstract class TextureBuildWorkerTarget : DerivedDataBuildWorkerTarget
{
	public TextureBuildWorkerTarget(TargetInfo Target) : base(Target)
	{
		SolutionDirectory += "/Texture";
	}
}

public class TextureBuildWorker : ModuleRules
{
	public TextureBuildWorker(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("DerivedDataCache");
		PrivateDependencyModuleNames.Add("DerivedDataBuildWorker");

		// Determine if TextureFormatOodle is enabled.
		var TextureFormatOodleUPluginFile = FileReference.Combine(new DirectoryReference(EngineDirectory), "Plugins/Developer/TextureFormatOodle/TextureFormatOodle.uplugin");
		var TextureFormatOodlePlugin = new PluginInfo(TextureFormatOodleUPluginFile, PluginType.Engine);

		bool bTextureFormatOodlePluginEnabled =
		Enum.GetValues(typeof(UnrealTargetConfiguration)).Cast<UnrealTargetConfiguration>().Any(config
				=> Plugins.IsPluginEnabledForTarget(TextureFormatOodlePlugin, null, Target.Platform, config, TargetType.Program));

		if (bTextureFormatOodlePluginEnabled)
		{
			PrivateDependencyModuleNames.Add("TextureFormatOodle");
		}
	}
}
