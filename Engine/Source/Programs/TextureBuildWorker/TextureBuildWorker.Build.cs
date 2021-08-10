// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Linq;
using EpicGames.Core;
using UnrealBuildTool;

// Abstract base class for texture worker targets.  Not a valid target by itself, hence it is not put into a *.target.cs file.
[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public abstract class TextureBuildWorkerTarget : DerivedDataBuildWorkerTarget
{
	[ConfigFile(ConfigHierarchyType.Engine, "TextureBuildWorker", "ProjectOodlePlugin")]
	public string ProjectOodlePlugin;

	[ConfigFile(ConfigHierarchyType.Engine, "TextureBuildWorker", "ProjectOodleTextureFormatModule")]
	public string ProjectOodleTextureFormatModule;

	public TextureBuildWorkerTarget(TargetInfo Target) : base(Target)
	{
		SolutionDirectory += "/Texture";

		AddOodleModule(Target);
	}

	private void AddOodleModule(TargetInfo Target)
	{
		FileReference OodleUPluginFile = new FileReference("../Plugins/Developer/TextureFormatOodle/TextureFormatOodle.uplugin");
		PluginType OodlePluginType = PluginType.Engine;
		string OodleTextureFormatModule = "TextureFormatOodle";
		if (Target.ProjectFile != null)
		{
			if (!String.IsNullOrEmpty(ProjectOodlePlugin))
			{
				if (!Path.IsPathRooted(ProjectOodlePlugin))
				{
					OodleUPluginFile = FileReference.FromString(Path.Combine(Target.ProjectFile.Directory.ToString(), ProjectOodlePlugin));
					OodlePluginType = PluginType.Project;
				}
			}

			if (!String.IsNullOrEmpty(ProjectOodleTextureFormatModule))
			{
				OodleTextureFormatModule = ProjectOodleTextureFormatModule;
			}
		}

		// Determine if TextureFormatOodle is enabled.
		var ProjectDesc = ProjectFile != null ? ProjectDescriptor.FromFile(ProjectFile) : null;
		var OodlePlugin = new PluginInfo(OodleUPluginFile, OodlePluginType);
		bool bOodlePluginEnabled =
		Plugins.IsPluginEnabledForTarget(OodlePlugin, ProjectDesc, Target.Platform, Target.Configuration, TargetType.Program);

		if (bOodlePluginEnabled)
		{
			ExtraModuleNames.Add(OodleTextureFormatModule);
		}
	}
}

public class TextureBuildWorker : ModuleRules
{
	public TextureBuildWorker(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("DerivedDataCache");
		PrivateDependencyModuleNames.Add("DerivedDataBuildWorker");
	}
}
