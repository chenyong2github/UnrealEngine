// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using AutomationTool;
using UnrealBuildTool;
using Tools.DotNETCommon;

[Help("Builds common tools used by the engine which are not part of typical editor or game builds. Useful when syncing source-only on GitHub.")]
[Help("platforms=<X>+<Y>+...", "Specifies on or more platforms to build for (defaults to the current host platform)")]
[Help("manifest=<Path>", "Writes a manifest of all the build products to the given path")]
public class BuildCommonTools_HTML5 : BuildCommand
{
	public override void ExecuteBuild()
	{
		LogInformation("************************* BuildCommonTools");

		List<UnrealTargetPlatform> Platforms = new List<UnrealTargetPlatform>();

		// Add all the platforms if specified
		if (ParseParam("allplatforms"))
		{
			Platforms = UnrealTargetPlatform.GetValidPlatforms().ToList();
		}
		else
		{
			// Get the list of platform names
			string[] PlatformNames = ParseParamValue("platforms", BuildHostPlatform.Current.Platform.ToString()).Split('+');

			// Parse the platforms
			foreach (string PlatformName in PlatformNames)
			{
				UnrealBuildTool.UnrealTargetPlatform Platform;
				if (!UnrealTargetPlatform.TryParse(PlatformName, out Platform))
				{
					throw new AutomationException("Unknown platform specified on command line - '{0}' - valid platforms are {1}", PlatformName, String.Join("/", UnrealTargetPlatform.GetValidPlatformNames()));
				}
				Platforms.Add(Platform);
			}
		}

		// Get the agenda
		List<string> ExtraBuildProducts = new List<string>();
		UE4Build.BuildAgenda Agenda = MakeAgenda(Platforms.ToArray(), ExtraBuildProducts);

		// Build everything. We don't want to touch version files for GitHub builds -- these are "programmer builds" and won't have a canonical build version
		UE4Build Builder = new UE4Build(this);
		Builder.Build(Agenda, InUpdateVersionFiles: false);

		// Add UAT and UBT to the build products
		Builder.AddUATFilesToBuildProducts();
		Builder.AddUBTFilesToBuildProducts();

		// Add all the extra build products
		foreach(string ExtraBuildProduct in ExtraBuildProducts)
		{
			Builder.AddBuildProduct(ExtraBuildProduct);
		}

		// Make sure all the build products exist
		UE4Build.CheckBuildProducts(Builder.BuildProductFiles);

		// Write the manifest if needed
		string ManifestPath = ParseParamValue("manifest");
		if(ManifestPath != null)
		{
			SortedSet<string> Files = new SortedSet<string>();
			foreach(string BuildProductFile in Builder.BuildProductFiles)
			{
				Files.Add(BuildProductFile);
			}
			File.WriteAllLines(ManifestPath, Files.ToArray());
		}
	}

	public static UE4Build.BuildAgenda MakeAgenda(UnrealBuildTool.UnrealTargetPlatform[] Platforms, List<string> ExtraBuildProducts)
	{
		// Create the build agenda
		UE4Build.BuildAgenda Agenda = new UE4Build.BuildAgenda();

		// HTML5 binaries
//		if (Platforms.Contains(UnrealBuildTool.UnrealTargetPlatform.HTML5))
		{
            Agenda.DotNetProjects.Add(@"Engine/Platforms/HTML5/Source/Programs/HTML5/HTML5LaunchHelper/HTML5LaunchHelper.csproj");
			ExtraBuildProducts.Add(CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, @"Engine/Binaries/DotNET/HTML5LaunchHelper.exe"));
		}

		return Agenda;
	}
}

