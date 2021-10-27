// Copyright Epic Games, Inc. All Rights Reserved.
using AutomationTool;
using EpicGames.Core;
using System;
using System.Diagnostics;
using System.Threading;

namespace AutomationScripts.Automation
{
	public class WorldPartitionBuilder : BuildCommand
	{
		public override void ExecuteBuild()
		{
			string Builder = ParseRequiredStringParam("Builder");
			string CommandletArgs = ParseOptionalStringParam("CommandletArgs");

			bool bSubmit = ParseParam("Submit");

			string ShelveUser = ParseOptionalStringParam("ShelveUser");
			string ShelveWorkspace = ParseOptionalStringParam("ShelveWorkspace");
			bool bShelveResult = !String.IsNullOrEmpty(ShelveUser) && !String.IsNullOrEmpty(ShelveWorkspace);

			if (!P4Enabled && (bSubmit || bShelveResult))
			{
				LogError("P4 required to submit or shelve build results");
				return;
			}

			CommandletArgs = "-Builder=" + Builder + " " + CommandletArgs;

			if (bSubmit)
			{
				CommandletArgs += " -Submit";
			}

			string EditorExe = "UnrealEditor-Cmd.exe";
			EditorExe = AutomationTool.HostPlatform.Current.GetUnrealExePath(EditorExe);

			FileReference ProjectPath = ParseProjectParam();
			RunCommandlet(ProjectPath, EditorExe, "WorldPartitionBuilderCommandlet", CommandletArgs);
			
			if (bShelveResult)
			{
				LogInformation("### Shelving build results ###");

				// Create a new changelist and move all checked out files to it
				LogInformation("Creating pending changelist to shelve builder changes");
				int PendingCL = P4.CreateChange(P4Env.Client);
				P4.LogP4("", $"reopen -c {PendingCL} //...", AllowSpew: true);

				// Shelve changelist & revert changes
				LogInformation("Shelving changes...");
				P4.Shelve(PendingCL);
				LogInformation("Reverting local changes...");
				P4.Revert($"-w -c {PendingCL} //...");

				// Assign shelve to the provided user+workspace
				LogInformation($"Changing ownership of CL {PendingCL} to user {ShelveUser}, workspace {ShelveWorkspace}");
				P4.UpdateChange(PendingCL, ShelveUser, ShelveWorkspace, null, true);

				LogInformation("### Shelving completed ###");
			}
		}
	}
}