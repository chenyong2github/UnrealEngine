// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Linq;
using AutomationTool;
using UnrealBuildTool;
using Tools.DotNETCommon;

[Help("Syncs and builds all the binaries required for a project")]
[Help("project=<FortniteGame>", "Project to sync. Will search current path and paths in ueprojectdirs. If omitted will sync projectdirs")]
[Help("threads=N", "How many threads to use when syncing. Default=2. When >1 all output happens first")]
[Help("cl=< 12345 >", "Changelist to sync to. If omitted will sync to latest CL of the workspace path")]
[Help("build", "Build after syncing")]
[Help("force", "force sync files (files opened for edit will be untouched)")]
[Help("preview", "Shows commands that will be executed but performs no operations")]
[Help("path", "Only sync files that match this path. Can be comma-separated list.")]
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
		string PathArg = ParseParamValue("path", "");

		IEnumerable<string> ExplicitPaths = PathArg.Split(new char[] { ',',';' }, StringSplitOptions.RemoveEmptyEntries).Select(S => S.Trim());

		if (ExplicitPaths.Any())
		{
			ExplicitPaths = ExplicitPaths.Select(P => {
				if (!P.EndsWith("...", StringComparison.OrdinalIgnoreCase))
				{
					P += "...";
				}
				return P;
			});
		}

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
		string ProjectFileName = ParseParamValue("Project", null);

		if(ProjectFileName == null)
		{
			string ProjectDirsFile = Directory.EnumerateFiles(CommandUtils.CombinePaths(P4Env.Branch, "*.uprojectdirs")).FirstOrDefault();

			if (ProjectDirsFile == null)
			{
				throw new AutomationException("Missing Project parameter");
			}

			ProjectFileName = ProjectDirsFile;
		}
		else
		{
			if (!File.Exists(ProjectFileName))
			{
				FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectFileName);

				if (ProjectFileName == null)
				{
					throw new AutomationException("Could not find project file for {0}", ProjectFileName);
				}

				ProjectFileName = ProjectFile.FullName;
			}
		}		

		// Check if it's a project file (versus a *.uprojectdirs file)
		bool bIsProjectFile= ProjectFileName.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase);
		
		// Figure out where the project is in Perforce
		P4WhereRecord ProjectFileRecord = P4.Where(ProjectFileName, AllowSpew: false).FirstOrDefault(x => x.DepotFile != null && !x.bUnmap);
		if(ProjectFileRecord == null)
		{
			throw new AutomationException("Couldn't find location of {0} in Perforce");
		}

		// Get the root of the branch containing the selected file
		string BranchRoot = CommandUtils.GetDirectoryName(ProjectFileRecord.DepotFile);
		for(;;)
		{
			string EngineTargetPath = CommandUtils.CombinePaths(PathSeparator.Slash, BranchRoot, "Engine", "Source", "UE4Editor.target.cs");

			if (P4.FileExistsInDepot(EngineTargetPath, false))
			{
				break;
			}

			int LastIndex = BranchRoot.LastIndexOf('/');
			if(LastIndex <= 2)
			{
				throw new AutomationException("Couldn't find branch root from {0}", ProjectFileRecord.DepotFile);
			}

			BranchRoot = BranchRoot.Substring(0, LastIndex);
		}

		// Build the list of paths that need syncing
		List<string> SyncPaths = new List<string>();

		if (!ExplicitPaths.Any())
		{
			if (bIsProjectFile)
			{
				SyncPaths.Add(CommandUtils.CombinePaths(PathSeparator.Slash, BranchRoot, "*"));
				SyncPaths.Add(CommandUtils.CombinePaths(PathSeparator.Slash, BranchRoot, "Engine", "..."));
				SyncPaths.Add(CommandUtils.CombinePaths(PathSeparator.Slash, CommandUtils.GetDirectoryName(ProjectFileRecord.DepotFile), "..."));
			}
			else
			{
				SyncPaths.Add(CommandUtils.CombinePaths(PathSeparator.Slash, CommandUtils.GetDirectoryName(ProjectFileRecord.DepotFile), "..."));
			}
		}
		else
		{
			SyncPaths.AddRange(ExplicitPaths);
		}

		// Force these files as they can be overwritten by tools
		if (!PreviewOnly)
		{
			IEnumerable<string> ForceSyncList = ForceSyncFiles.Select(F => CommandUtils.CombinePaths(PathSeparator.Slash, BranchRoot, F));

			foreach (var F in ForceSyncList)
			{
				LogInformation("Force-updating {0}", F);

				string SyncCommand = string.Format("-f {0}@{1}", F, CL);

				// sync with retries
				P4.Sync(SyncCommand, true, false, 3, 30);
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
				P4.Sync(SyncCommand, true, false, 3, 30);
			}
			else
			{
				LogInformation("sync {0}", SyncCommand);
			}
		}

		ExitCode ExitStatus = ExitCode.Success;

		if (!PreviewOnly)
		{
			UE4Build Build = new UE4Build(this);
			Build.UpdateVersionFiles(ActuallyUpdateVersionFiles: true, ChangelistNumberOverride: CL, IsPromotedOverride: false);

			// Build everything
			if (ParseParam("build") && ExitStatus == ExitCode.Success)
			{
				BuildEditor BuildCmd = new BuildEditor();
				BuildCmd.ProjectName = ProjectFileName;
				ExitStatus = BuildCmd.Execute();
			}

			if (ParseParam("open") && ExitStatus == ExitCode.Success)
			{
				OpenEditor OpenCmd = new OpenEditor();
				OpenCmd.ProjectName = ProjectFileName;
				ExitStatus = OpenCmd.Execute();
			}

		}

		return ExitCode.Success;
	}
}
