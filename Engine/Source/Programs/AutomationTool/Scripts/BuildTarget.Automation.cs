// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool
{

	[Help("Builds the specified targets and configurations for the specified project.")]
	[Help("Example BuildTarget -project=QAGame -target=Editor+Game -platform=PS4+XboxOne -configuration=Development.")]
	[Help("Note: Editor will only ever build for the current platform in a Development config and required tools will be included")]
	[Help("project=<QAGame>", "Project to build. Will search current path and paths in ueprojectdirs. If omitted will build vanilla UE4Editor")]
	[Help("platform=PS4+XboxOne", "Platforms to build, join multiple platforms using +")]
	[Help("configuration=Development+Test", "Configurations to build, join multiple configurations using +")]
	[Help("target=Editor+Game", "Targets to build, join multiple targets using +")]
	[Help("notools", "Don't build any tools (UHT, ShaderCompiler, CrashReporter")]
	class BuildTarget : BuildCommand
	{
		// exposed as a property so projects can derive and set this directly
		public string ProjectName { get; set; }

		public string Targets { get; set; }

		public string Platforms { get; set; }

		public string Configurations { get; set; }

		protected Dictionary<string, string> TargetNames { get; set; }

		public BuildTarget()
		{
			Platforms = HostPlatform.Current.HostEditorPlatform.ToString();
			Configurations = "Development";

			TargetNames = new Dictionary<string, string>();
		}

		public override ExitCode Execute()
		{
			FileReference ProjectFile = null;

			string[] Arguments = this.Params;

			ProjectName = ParseParamValue("project", ProjectName);
			Targets = ParseParamValue("target", Targets);
			Platforms = ParseParamValue("platform", Platforms);
			Configurations = ParseParamValue("configuration", Configurations);

			bool NoTools = ParseParam("notools");

			IEnumerable<string> TargetList = Targets.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries);
			IEnumerable<UnrealTargetConfiguration> ConfigurationList = null;
			IEnumerable<UnrealTargetPlatform> PlatformList = null;

			try
			{
				ConfigurationList = Configurations.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries)
																		.Select(C => (UnrealTargetConfiguration)Enum.Parse(typeof(UnrealTargetConfiguration), C, true)).ToArray();
			}
			catch (Exception Ex)
			{
				LogError("Failed to parse configuration string. {0}", Ex.Message);
				return ExitCode.Error_Arguments;
			}

			try
			{
				PlatformList = Platforms.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries)
																		.Select(C =>
																		{
																			UnrealTargetPlatform Platform;
																			if (!UnrealTargetPlatform.TryParse(C, out Platform))
																			{
																				throw new AutomationException("No such platform {0}", C);
																			}
																			return Platform;
																		}).ToArray();
			}
			catch (Exception Ex)
			{
				LogError("Failed to parse configuration string. {0}", Ex.Message);
				return ExitCode.Error_Arguments;
			}

			if (String.IsNullOrEmpty(ProjectName))
			{
				Log.TraceWarning("No project specified, will build vanilla UE4 binaries");
			}
			else
			{
				ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

				if (ProjectFile == null)
				{
					throw new AutomationException("Unable to find uproject file for {0}", ProjectName);
				}

				string SourceDirectoryName = Path.Combine(ProjectFile.Directory.FullName, "Source");

				IEnumerable<string> TargetScripts = Directory.EnumerateFiles(SourceDirectoryName, "*.Target.cs");

				foreach (string TargetName in TargetList)
				{
					string TargetScript = TargetScripts.Where(S => S.IndexOf(TargetName, StringComparison.OrdinalIgnoreCase) >= 0).FirstOrDefault();

					if (TargetScript == null && (
							!TargetName.Equals("Client", StringComparison.OrdinalIgnoreCase) ||
							!TargetName.Equals("Game", StringComparison.OrdinalIgnoreCase)
							)
						)
					{
						// if there's no ProjectGame.Target.cs or ProjectClient.Target.cs then
						// fallback to Project.Target.cs
						TargetScript = TargetScripts.Where(S => S.IndexOf(ProjectName + ".", StringComparison.OrdinalIgnoreCase) >= 0).FirstOrDefault();
					}

					if (TargetScript == null)
					{
						throw new AutomationException("No Target.cs file for target {0} in project {1}", TargetName, ProjectName);
					}

					string FullName = Path.GetFileName(TargetScript);
					TargetNames[TargetName] = Regex.Replace(FullName, ".Target.cs", "", RegexOptions.IgnoreCase);
				}
			}

			UE4Build Build = new UE4Build(this);

			UE4Build.BuildAgenda Agenda = new UE4Build.BuildAgenda();

			string EditorTarget = TargetList.Where(T => T.EndsWith("Editor", StringComparison.OrdinalIgnoreCase)).FirstOrDefault();

			IEnumerable<string> OtherTargets = TargetList.Where(T => T != EditorTarget);

			UnrealTargetPlatform CurrentPlatform = HostPlatform.Current.HostEditorPlatform;

			if (!NoTools)
			{
				Agenda.AddTarget("UnrealHeaderTool", CurrentPlatform, UnrealTargetConfiguration.Development);
			}

			if (string.IsNullOrEmpty(EditorTarget) == false)
			{
				string TargetName = TargetNames[EditorTarget];

				Agenda.AddTarget(TargetName, CurrentPlatform, UnrealTargetConfiguration.Development, ProjectFile);

				if (!NoTools)
				{
					Agenda.AddTarget("ShaderCompileWorker", CurrentPlatform, UnrealTargetConfiguration.Development);
					Agenda.AddTarget("UnrealLightmass", CurrentPlatform, UnrealTargetConfiguration.Development);
					Agenda.AddTarget("UnrealPak", CurrentPlatform, UnrealTargetConfiguration.Development);
					Agenda.AddTarget("CrashReportClient", CurrentPlatform, UnrealTargetConfiguration.Shipping);
				}
			}

			foreach (string Target in OtherTargets)
			{
				string TargetName = TargetNames[Target];

				bool IsServer = Target.EndsWith("Server", StringComparison.OrdinalIgnoreCase);

				IEnumerable<UnrealTargetPlatform> PlatformsToBuild = IsServer ? new UnrealTargetPlatform[] { CurrentPlatform } : PlatformList;

				foreach (UnrealTargetPlatform Platform in PlatformsToBuild)
				{
					foreach (UnrealTargetConfiguration Config in ConfigurationList)
					{
						Agenda.AddTarget(TargetName, Platform, Config);
					}
				}
			}

			foreach (var Target in Agenda.Targets)
			{
				Log.TraceInformation("Will build {0}", Target);
			}

			Build.Build(Agenda, InUpdateVersionFiles: false);

			return ExitCode.Success;
		}
	}

	[Help("Builds the editor for the specified project.")]
	[Help("Example BuildEditor -project=QAGame")]
	[Help("Note: Editor will only ever build for the current platform in a Development config and required tools will be included")]
	[Help("project=<QAGame>", "Project to build. Will search current path and paths in ueprojectdirs. If omitted will build vanilla UE4Editor")]
	[Help("notools", "Don't build any tools (UHT, ShaderCompiler, CrashReporter")]
	class BuildEditor : BuildTarget
	{
		public BuildEditor()
		{
			Targets = "Editor";
		}

		public override ExitCode Execute()
		{
			bool DoOpen = ParseParam("open");
			ExitCode Status = base.Execute();

			if (Status == ExitCode.Success && DoOpen)
			{
				OpenEditor OpenCmd = new OpenEditor();
				OpenCmd.ProjectName = this.ProjectName;
				Status = OpenCmd.Execute();
			}

			return Status;			
		}
	}

	[Help("Builds the game for the specified project.")]
	[Help("Example BuildGame -project=QAGame -platform=PS4+XboxOne -configuration=Development.")]
	[Help("project=<QAGame>", "Project to build. Will search current path and paths in ueprojectdirs.")]
	[Help("platform=PS4+XboxOne", "Platforms to build, join multiple platforms using +")]
	[Help("configuration=Development+Test", "Configurations to build, join multiple configurations using +")]
	[Help("notools", "Don't build any tools (UHT, ShaderCompiler, CrashReporter")]
	class BuildGame : BuildTarget
	{
		public BuildGame()
		{
			Targets = "Game";
		}
	}

	[Help("Builds the server for the specified project.")]
	[Help("Example BuildServer -project=QAGame -platform=Win64 -configuration=Development.")]
	[Help("project=<QAGame>", "Project to build. Will search current path and paths in ueprojectdirs.")]
	[Help("platform=Win64", "Platforms to build, join multiple platforms using +")]
	[Help("configuration=Development+Test", "Configurations to build, join multiple configurations using +")]
	[Help("notools", "Don't build any tools (UHT, ShaderCompiler, CrashReporter")]
	class BuildServer : BuildTarget
	{
		public BuildServer()
		{
			Targets = "Server";
		}
	}
}

