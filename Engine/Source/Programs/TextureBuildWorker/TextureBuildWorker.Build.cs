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

		var ProjectDesc = ProjectFile != null ? ProjectDescriptor.FromFile(ProjectFile) : null;

		// Determine if TextureFormatOodle is enabled.
		var TextureFormatOodleUPluginFile = new FileReference("../Plugins/Developer/TextureFormatOodle/TextureFormatOodle.uplugin");
		var TextureFormatOodlePlugin = new PluginInfo(TextureFormatOodleUPluginFile, PluginType.Engine);

		bool bTextureFormatOodlePluginEnabled =
		Plugins.IsPluginEnabledForTarget(TextureFormatOodlePlugin, ProjectDesc, Target.Platform, Target.Configuration, TargetType.Program);

		if (bTextureFormatOodlePluginEnabled)
		{
			ExtraModuleNames.Add("TextureFormatOodle");
		}
		else
		{
			// Check for a project specific Oodle plugin
			foreach (PluginReferenceDescriptor PluginReference in ProjectDesc.Plugins)
			{
				if (String.Compare(PluginReference.Name, "Oodle", true) == 0 && !PluginReference.bOptional)
				{
					ExtraModuleNames.Add("OodleDXTTextureFormat");
					break;
				}
			}
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
