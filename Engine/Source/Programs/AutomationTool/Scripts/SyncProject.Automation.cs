// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Linq;
using AutomationTool;
using UnrealBuildTool;
using Tools.DotNETCommon;
using System.Text.RegularExpressions;

[Help("Syncs and builds all the binaries required for a project")]
[Help("project=<FortniteGame>", "Project to sync. Will search current path and paths in ueprojectdirs. If omitted will sync projectdirs")]
[Help("threads=N", "How many threads to use when syncing. Default=2. When >1 all output happens first")]
[Help("cl=< 12345 >", "Changelist to sync to. If omitted will sync to latest CL of the workspace path")]
[Help("build", "Build after syncing")]
[Help("open", "Open project editor after syncing")]
[Help("generate", "Generate project files after syncing")]
[Help("force", "force sync files (files opened for edit will be untouched)")]
[Help("preview", "Shows commands that will be executed but performs no operations")]
[Help("projectonly", "Only shync the project")]
[Help("maxwait=n", "Maximum wait time for a file. Defaults to 0 (disabled).")]
[Help("retries=n", "Number of retries for a timed out file. Defaults to 3")]
[Help("unversioned", "Do not set an engine version after syncing")]
[RequireP4]
[DoesNotNeedP4CL]
class SyncProject : BuildCommand
{
	public override ExitCode Execute()
	{
		LogInformation("************************* SyncProject");

		// These are files that should always be synced because tools update them
		string[] ForceSyncFiles = new string[]
		{
			"Engine/Build/Build.version",
			"Engine/Source/Programs/DotNETCommon/MetaData.cs"
		};

		// Parse the changelist to sync to. -1 will sync to head.
		int CL = ParseParamInt("CL", -1);
		int Threads = ParseParamInt("threads", 2);
		bool ForceSync = ParseParam("force");
		bool PreviewOnly = ParseParam("preview");
		bool ProjectOnly = ParseParam("projectonly");
		int MaxWait = ParseParamInt("maxwait", 0);
		int Retries = ParseParamInt("retries", 3);
		bool Unversioned = ParseParam("unversioned");

		bool BuildProject = ParseParam("build");
		bool OpenProject = ParseParam("open");
		bool GenerateProject = ParseParam("generate");

		if (CL == 0)
		{
			throw new AutomationException("If specified CL must be a number. Omit -CL for latest.");
		}
		else if (CL == -1)
		{
			// find most recent change
			List<P4Connection.ChangeRecord> Records;

			string Cmd = string.Format("-s submitted -m1 //{0}/...", P4Env.Client);
			if (!P4.Changes(out Records, Cmd, false))
			{
				throw new AutomationException("p4 changes failed. Cannot find most recent CL");
			}

			CL = Records.First().CL;
		}

		// Parse the project filename (as a local path)
		string ProjectName = ParseParamValue("Project", null);

		// this will be the path to the project in P4, if one is specified
		string P4ProjectPath = null;

		if (!string.IsNullOrEmpty(ProjectName))
		{
			// We want to find the Perforce path to the project so we can sync this even if it doesn't
			// exist locally

			// First try to find a local file that exists
			FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

			if (ProjectFile != null)
			{
				// get the path in P4
				P4WhereRecord Record = P4.Where(ProjectFile.FullName, AllowSpew: false).FirstOrDefault(x => x.DepotFile != null && !x.bUnmap);
				P4ProjectPath = Record.ClientFile;
			}
			else
			{
				// Couldn't be find locally, so try to find where it is in P4 by checking workspace root and uproject dirs
				// ok, check P4...
				string RelativePath = ProjectName;

				// Change EngineTest into EngineTest.uproject
				if (Path.GetExtension(RelativePath).Length == 0)
				{
					RelativePath = Path.ChangeExtension(RelativePath, "uproject");
				}

				// Change EngineTest.uproject into EngineTest/EngineTest.uproject
				if (!RelativePath.Contains(Path.DirectorySeparatorChar) && !RelativePath.Contains(Path.AltDirectorySeparatorChar))
				{
					RelativePath = CommandUtils.CombinePaths(PathSeparator.Slash, Path.GetFileNameWithoutExtension(RelativePath), RelativePath);
				}

				// the calls to P4.FileExists will outout text regardless of spew setting so tell
				// people why the are going to see these...
				Log.TraceInformation("{0} not on disk. Searching P4 for {1}", ProjectName, RelativePath);

				List<string> SearchPaths = new List<string>();

				// Search workspace root
				SearchPaths.Add("");

				// now check uproject dirs
				string ProjectDirsFile = Directory.EnumerateFiles(CommandUtils.CombinePaths(CmdEnv.LocalRoot), "*.uprojectdirs").FirstOrDefault();

				if (ProjectDirsFile != null)
				{
					// read the project dirs file, removing anything invalid and the cur-path entry
					// we add ourselves
					foreach (string FilePath in File.ReadAllLines(ProjectDirsFile))
					{
						string Trimmed = FilePath.Trim();
						if (!Trimmed.StartsWith("./", StringComparison.OrdinalIgnoreCase) &&
							!Trimmed.StartsWith(";", StringComparison.OrdinalIgnoreCase) &&
							Trimmed.IndexOfAny(Path.GetInvalidPathChars()) < 0)
						{
							SearchPaths.Add(Trimmed);
						}
					}
				}

				// now check P4
				foreach (string SearchPath in SearchPaths)
				{
					string P4Path = CommandUtils.CombinePaths(PathSeparator.Slash, P4Env.ClientRoot, SearchPath, RelativePath);

					if (P4.FileExistsInDepot(P4Path))
					{
						P4WhereRecord Record = P4.Where(P4Path, AllowSpew: false).FirstOrDefault(x => x.DepotFile != null && !x.bUnmap);
						P4ProjectPath = Record.DepotFile;
						Log.TraceInformation("Found project at {0}", P4ProjectPath);
						break;
					}
				}

				if (string.IsNullOrEmpty(P4ProjectPath))
				{
					throw new AutomationException("Could not find project file for {0} locally or in P4. Provide a full path or check the subdirectory is listed in UE4Games.uprojectdirs", ProjectName);
				}
			}
		}

		// Build the list of paths that need syncing
		List<string> SyncPaths = new List<string>();

		// See if the engine is in P4 too by checking the p4 location of a local file
		string LocalEngineFile = CommandUtils.CombinePaths(CmdEnv.LocalRoot, "Engine", "Source", "UE4Editor.target.cs");
		P4WhereRecord EngineRecord = P4.Where(LocalEngineFile, AllowSpew: false).FirstOrDefault(x => x.DepotFile != null && !x.bUnmap);

		string P4EngineLocation = null;

		// add engine files if they exist
		if (!ProjectOnly && P4.FileExistsInDepot(EngineRecord.DepotFile))
		{
			P4EngineLocation = EngineRecord.DepotFile.Replace("Engine/Source/UE4Editor.target.cs", "");
			SyncPaths.Add(CommandUtils.CombinePaths(PathSeparator.Slash, P4EngineLocation + "*"));
			SyncPaths.Add(CommandUtils.CombinePaths(PathSeparator.Slash, P4EngineLocation, "Engine", "..."));
		}

		// add project file if any
		if (!string.IsNullOrEmpty(P4ProjectPath))
		{
			string P4ProjectDir = Regex.Replace(P4ProjectPath, @"[^/]+\.uproject", "...", RegexOptions.IgnoreCase);
			SyncPaths.Add(P4ProjectDir);
		}

		// Force these files as they can be overwritten by tools
		if (!PreviewOnly && !string.IsNullOrEmpty(P4EngineLocation))
		{
			IEnumerable<string> ForceSyncList = ForceSyncFiles.Select(F => CommandUtils.CombinePaths(PathSeparator.Slash, P4EngineLocation, F));

			foreach (var F in ForceSyncList)
			{
				LogInformation("Force-updating {0}", F);

				string SyncCommand = string.Format("-f {0}@{1}", F, CL);

				// sync with retries
				P4.Sync(SyncCommand, true, false, Retries, MaxWait);
			}
		}

		// Sync all the things
		foreach(string SyncPath in SyncPaths)
		{
			string SyncCommand = "";

			if (Threads > 1)
			{
				SyncCommand = string.Format(" --parallel \"threads={0}\" {1}", Threads, SyncCommand);
			}

			if (ForceSync)
			{
				SyncCommand = SyncCommand + " -f";
			}

			SyncCommand += string.Format(" {0}@{1}", SyncPath, CL);

			if (!PreviewOnly)
			{
				// sync with retries
				P4.Sync(SyncCommand, true, false, Retries, MaxWait);
			}
			else
			{
				LogInformation("sync {0}", SyncCommand);
			}
		}

		ExitCode ExitStatus = ExitCode.Success;

		if (!PreviewOnly)
		{
			//  Get a reference to the project file
			FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

			if (GenerateProject)
			{
				// generate project files.
				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64 ||
					BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win32)
				{
					CommandUtils.Run("GenerateProjectFiles.bat", ProjectFile.FullName);
				}
				else
				{
					CommandUtils.Run("GenerateProjectFiles.sh", ProjectFile.FullName);
				}
			}

			UE4Build Build = new UE4Build(this);

			if (!Unversioned)
			{
				Build.UpdateVersionFiles(ActuallyUpdateVersionFiles: true, ChangelistNumberOverride: CL, IsPromotedOverride: false);
			}

			// Build everything
			if (BuildProject && ExitStatus == ExitCode.Success)
			{
				BuildEditor BuildCmd = new BuildEditor();
				BuildCmd.ProjectName = ProjectName;
				ExitStatus = BuildCmd.Execute();
			}

			if (OpenProject && ExitStatus == ExitCode.Success)
			{
				OpenEditor OpenCmd = new OpenEditor();
				OpenCmd.ProjectName = ProjectName;
				ExitStatus = OpenCmd.Execute();
			}

		}

		return ExitCode.Success;
	}
}
