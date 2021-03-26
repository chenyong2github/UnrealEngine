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

		public bool Preview { get; set; }

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
			Preview = ParseParam("preview") || Preview;

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

			if (!string.IsNullOrEmpty(ProjectName))
			{
				// find the project
				ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);				

				if (ProjectFile == null)
				{
					throw new AutomationException("Unable to find uproject file for {0}", ProjectName);
				}
			}

			IEnumerable<string> BuildTargets = TargetList.Select(T => ProjectTargetFromTarget(T, ProjectFile, PlatformList, ConfigurationList)).ToArray();			

			bool ContainsEditor = BuildTargets.Where(T => T.EndsWith("Editor", StringComparison.OrdinalIgnoreCase)).Any();
			bool SingleBuild = BuildTargets.Count() == 1 && PlatformList.Count() == 1 && ConfigurationList.Count() == 1;

			if (!SingleBuild || (ContainsEditor && !NoTools))
			{
				UE4Build Build = new UE4Build(this);
				Build.AlwaysBuildUHT = true;

				UE4Build.BuildAgenda Agenda = new UE4Build.BuildAgenda();

				string EditorTarget = BuildTargets.Where(T => T.EndsWith("Editor", StringComparison.OrdinalIgnoreCase)).FirstOrDefault();

				IEnumerable<string> OtherTargets = BuildTargets.Where(T => T != EditorTarget);

				UnrealTargetPlatform CurrentPlatform = HostPlatform.Current.HostEditorPlatform;

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

				foreach (var Target in Agenda.Targets)
				{
					Log.TraceInformation("Will {0}build {1}", Clean ? "clean and " : "", Target);
					if (Clean)
					{
						Target.Clean = Clean;
					}
				}

				if (!Preview)
				{
					Build.Build(Agenda, InUpdateVersionFiles: false);
				}
			}
			else
			{
				// Get the path to UBT
				FileReference InstalledUBT = FileReference.Combine(CommandUtils.EngineDirectory, "Binaries", "DotNET", "UnrealBuildTool.exe");

				UnrealTargetPlatform PlatformToBuild = PlatformList.First();
				UnrealTargetConfiguration ConfigToBuild = ConfigurationList.First();
				string TargetToBuild = BuildTargets.First();

				if (!Preview)
				{
					// Compile the editor
					string CommandLine = CommandUtils.UBTCommandline(ProjectFile, TargetToBuild, PlatformToBuild, ConfigToBuild, UBTArgs);

					if (Clean)
					{
						CommandUtils.RunUBT(CommandUtils.CmdEnv, InstalledUBT.FullName, CommandLine + " -clean");
					}
					CommandUtils.RunUBT(CommandUtils.CmdEnv, InstalledUBT.FullName, CommandLine);
				}
				else
				{ 
					Log.TraceInformation("Will {0}build {1} {2} {3}", Clean ? "clean and " : "", TargetToBuild, PlatformToBuild, ConfigToBuild);
				}
				
			}

			return ExitCode.Success;
		}

		public string ProjectTargetFromTarget(string InTargetName, FileReference InProjectFile, IEnumerable<UnrealTargetPlatform> InPlatformList, IEnumerable<UnrealTargetConfiguration> InConfigurationList)
		{
			ProjectProperties Properties = InProjectFile != null ? ProjectUtils.GetProjectProperties(InProjectFile, InPlatformList.ToList(), InConfigurationList.ToList()) : null;

			string ProjectTarget = null;

			if (Properties!= null && Properties.bIsCodeBasedProject)
			{
				var AvailableTargets = Properties.Targets.Select(T => T.Rules.Type.ToString());

				// go through the list of targets such as Editor, Client, Server etc and replace them with their real target names
				List<string> ActualTargets = new List<string>();

				// If they asked for ShooterClient etc and that's there, just return that.
				if (Properties.Targets.Any(T => T.TargetName.Equals(InTargetName, StringComparison.OrdinalIgnoreCase)))
				{
					ProjectTarget = InTargetName;
				}
				else
				{
					// find targets that match (and there may be multiple...)
					IEnumerable<string> MatchingTargetTypes = Properties.Targets.Where(T => T.Rules.Type.ToString().Equals(InTargetName, StringComparison.OrdinalIgnoreCase)).Select(T => T.TargetName);

					if (MatchingTargetTypes.Any())
					{
						if (MatchingTargetTypes.Count() == 1)
						{
							ProjectTarget = MatchingTargetTypes.First();
						}
						else
						{
							// if multiple targets, pick the one with our name (FN specific!)
							ProjectTarget = MatchingTargetTypes.Where(T => string.CompareOrdinal(T, 0, ProjectName, 0, 1) == 0).FirstOrDefault();
						}
					}
				}
			}
			else
			{
				// default UE4 targets
				IEnumerable<string> UE4Targets = new[] { "Editor", "Game", "Client", "Server" };

				string ShortTargetName = InTargetName;
				if (ShortTargetName.StartsWith("UE4", StringComparison.OrdinalIgnoreCase))
				{
					ShortTargetName = ShortTargetName.Substring(3);
				}

				string UE4Target = UE4Targets.Where(S => S.Equals(ShortTargetName, StringComparison.OrdinalIgnoreCase)).FirstOrDefault();
	
				// If they asked for editor, client etc then give them the UE version
				if (!string.IsNullOrEmpty(UE4Target))
				{
					ProjectTarget = "UE4" + UE4Target;
				}
				else
				{
					// or just build what they want and let later code figure out if that's valid. E.g. "UnrealPak"
					ProjectTarget = InTargetName;
				}
			}

			if (string.IsNullOrEmpty(ProjectTarget))
			{
				throw new AutomationException("{0} is not a valid target in {1}", InTargetName, InProjectFile);
			}
		
			return ProjectTarget;
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

