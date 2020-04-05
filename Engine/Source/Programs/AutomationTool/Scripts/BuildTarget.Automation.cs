// Copyright Epic Games, Inc. All Rights Reserved.

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
	[Help("notools", "Don't build any tools (UnrealPak, Lightmass, ShaderCompiler, CrashReporter")]
	[Help("clean", "Do a clean build")]
	[Help("NoXGE", "Toggle to disable the distributed build process")]
	[Help("DisableUnity", "Toggle to disable the unity build system")]
	public class BuildTarget : BuildCommand
	{
		// exposed as a property so projects can derive and set this directly
		public string ProjectName { get; set; }

		public string Targets { get; set; }

		public string Platforms { get; set; }

		public string Configurations { get; set; }

		public bool	  Clean { get; set; }

		public bool	  NoTools { get; set; }

		public string UBTArgs { get; set; }

		public BuildTarget()
		{
			Platforms = HostPlatform.Current.HostEditorPlatform.ToString();
			Configurations = "Development";
			UBTArgs = "";
		}

		public override ExitCode Execute()
		{
			string[] Arguments = this.Params;

			ProjectName = ParseParamValue("project", ProjectName);
			Targets = ParseParamValue("target", Targets);
			Platforms = ParseParamValue("platform", Platforms);
			Configurations = ParseParamValue("configuration", Configurations);
			Clean = ParseParam("clean") || Clean;
			NoTools = ParseParam("NoTools") || NoTools;
			UBTArgs = ParseParamValue("ubtargs", UBTArgs);

			if (string.IsNullOrEmpty(Targets))
			{
				throw new AutomationException("No target specified with -target. Use -help to see all options");
			}

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

			FileReference ProjectFile = null;

			// default UE4 targets
			IEnumerable<string> AvailableTargets = new[] { "editor", "game", "client", "server" };

			bool RequireProjectTargets = false;

			if (!string.IsNullOrEmpty(ProjectName))
			{
				// find the project
				ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);				

				if (ProjectFile == null)
				{
					throw new AutomationException("Unable to find uproject file for {0}", ProjectName);
				}

				// user may have passed a full path, we just want the name
				ProjectName = ProjectFile.GetFileNameWithoutAnyExtensions();

				ProjectProperties Properties = ProjectUtils.GetProjectProperties(ProjectFile);

				if (Properties.bIsCodeBasedProject)
				{
					RequireProjectTargets = true;

					AvailableTargets = Properties.Targets.Select(T => T.Rules.Type.ToString());

					// go through the list of targets such as Editor, Client, Server etc and replace them with their real target names
					List<string> ActualTargets = new List<string>();
					foreach (string Target in TargetList)
					{
						// Could be Client, but could also be ShooterClient if that's what was explicitly asked for
						if (Properties.Targets.Any(T => T.TargetName.Equals(Target, StringComparison.OrdinalIgnoreCase)))
						{
							ActualTargets.Add(Target);
						}
						else
						{
							// find targets that match (and there may be multiple...)
							IEnumerable<string> MatchingTargetTypes = Properties.Targets.Where(T => T.Rules.Type.ToString().Equals(Target, StringComparison.OrdinalIgnoreCase)).Select(T => T.TargetName);

							if (!MatchingTargetTypes.Any())
							{
								string ValidTargets = string.Join(",", Properties.Targets.Select(T => T.Rules.Type.ToString()));
								throw new AutomationException("'{0}' is not a valid target for {1}. Targets are {2}", Target, ProjectName, ValidTargets);
							}

							string ProjectTarget;

							if (MatchingTargetTypes.Count() == 1)
							{
								ProjectTarget = MatchingTargetTypes.First();
							}
							else
							{
								// if multiple targets, pick the one with our name (FN specific!)
								ProjectTarget = MatchingTargetTypes.Where(T => string.CompareOrdinal(T, 0, ProjectName, 0, 1) == 0).FirstOrDefault();
							}

							if (ProjectTarget == null)
							{
								throw new AutomationException("Unable to find project target for {0}", Target);
							}

							ActualTargets.Add(ProjectTarget);
						}
					}

					TargetList = ActualTargets;
				}
			}

			if (!RequireProjectTargets)
			{
				Log.TraceInformation("Will build vanilla UE4 targets");

				List<string> ActualTargets = new List<string>();

				foreach (string Target in TargetList)
				{
					// If they asked for editor, client etc then give them the UE version
					if (AvailableTargets.Contains(Target.ToLower()))
					{
						ActualTargets.Add("UE4" + Target);
					}
					else
					{
						// or just build what they want and let later code figure out if that's valid. E.g. "UnrealPak"
						ActualTargets.Add(Target);
					}
				}

				TargetList = ActualTargets;
			}

			UE4Build Build = new UE4Build(this);
			Build.AlwaysBuildUHT = true;

			UE4Build.BuildAgenda Agenda = new UE4Build.BuildAgenda();

			string EditorTarget = TargetList.Where(T => T.EndsWith("Editor", StringComparison.OrdinalIgnoreCase)).FirstOrDefault();

			IEnumerable<string> OtherTargets = TargetList.Where(T => T != EditorTarget);

			UnrealTargetPlatform CurrentPlatform = HostPlatform.Current.HostEditorPlatform;

			//if (!NoTools)
			{
				//Agenda.AddTarget("UnrealHeaderTool", CurrentPlatform, UnrealTargetConfiguration.Development, ProjectFile);
			}

			if (string.IsNullOrEmpty(EditorTarget) == false)
			{
				Agenda.AddTarget(EditorTarget, CurrentPlatform, UnrealTargetConfiguration.Development, ProjectFile, UBTArgs);

				if (!NoTools)
				{
					Agenda.AddTarget("UnrealPak", CurrentPlatform, UnrealTargetConfiguration.Development, ProjectFile, UBTArgs);
					Agenda.AddTarget("ShaderCompileWorker", CurrentPlatform, UnrealTargetConfiguration.Development, ProjectFile, UBTArgs);
					Agenda.AddTarget("UnrealLightmass", CurrentPlatform, UnrealTargetConfiguration.Development, ProjectFile, UBTArgs);
					Agenda.AddTarget("CrashReportClient", CurrentPlatform, UnrealTargetConfiguration.Shipping, ProjectFile, UBTArgs);
					Agenda.AddTarget("CrashReportClientEditor", CurrentPlatform, UnrealTargetConfiguration.Shipping, ProjectFile, UBTArgs);
				}
			}

			foreach (string Target in OtherTargets)
			{
				bool IsServer = Target.EndsWith("Server", StringComparison.OrdinalIgnoreCase);

				IEnumerable<UnrealTargetPlatform> PlatformsToBuild = IsServer ? new UnrealTargetPlatform[] { CurrentPlatform } : PlatformList;

				foreach (UnrealTargetPlatform Platform in PlatformsToBuild)
				{
					foreach (UnrealTargetConfiguration Config in ConfigurationList)
					{
						Agenda.AddTarget(Target, Platform, Config, ProjectFile, UBTArgs);
					}
				}
			}

			// Set clean and log
			foreach (var Target in Agenda.Targets)
			{
				if (Clean)
				{
					Target.Clean = Clean;
				}

				Log.TraceInformation("Will {0}build {1}", Clean ? "clean and " : "", Target);
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

