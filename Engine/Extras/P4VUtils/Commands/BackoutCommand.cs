// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using P4VUtils.Perforce;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;

namespace P4VUtils.Commands
{
	class BackoutValidationResult
	{
		public bool Success { get; set; } = true;
		public String Reason { get; set; } = "";
	}

	class BackoutCommand : Command
	{
		public override string Description => "P4 Admin sanctioned method of backing out a CL";

		public override CustomToolInfo CustomTool => new CustomToolInfo("Safe Backout tool", "%S") { ShowConsole = true }; 

		private static async Task<BackoutValidationResult> ValidateBackoutSafety(PerforceConnection Perforce, int Change, ILogger Logger)
		{
			BackoutValidationResult Result = new BackoutValidationResult();

			Logger.LogInformation("Examining CL {Change} for Safe Backout", Change);

			// was this change cherry picked using ushell or p4vutil or was it robomerged?
			DescribeRecord DescribeRec = await Perforce.DescribeAsync(Change, CancellationToken.None);
			if (DescribeRec.Description.Contains("#ushell-cherrypick", StringComparison.InvariantCultureIgnoreCase)
			|| DescribeRec.Description.Contains("#p4v-cherrypick", StringComparison.InvariantCultureIgnoreCase)
			|| DescribeRec.Description.Contains("#ROBOMERGE-SOURCE", StringComparison.InvariantCultureIgnoreCase))
			{
				// it was a cherry pick or a robomerged CL, just bail
				Logger.LogError("CL {Change} did not originate in the current stream,\nto be safe, CLs should be backed out where the change was originally submitted", Change);
				Result.Success = false;
				Result.Reason = string.Format($"CL {Change} did not originate in the current stream, to be safe, CLs should be backed out where the change was originally submitted.", Change);
				return Result;
			}

			foreach (DescribeFileRecord DescFileRec in DescribeRec.Files)
			{
				// is this an edit/add/delete operation?
				if (!P4ActionGroups.EditActions.Contains(DescFileRec.Action))
				{
					Logger.LogError("In CL {Change},\nthe action for the file listed below isn't considered an 'edit',\ntherefore in isn't safe to back it out.\n\n{FileName}", Change, DescFileRec.DepotFile);
					Result.Success = false;
					Result.Reason = $"In CL {Change},\nthe action for the file listed below isn't considered an 'edit', therefore in isn't safe to back it out.\n\n{DescFileRec.DepotFile}";
					return Result;
				}

				// is it an versioning file?
				if (DescFileRec.DepotFile.Contains("ObjectVersion.h", StringComparison.InvariantCultureIgnoreCase)
					|| DescFileRec.DepotFile.Contains("CustomVersion.h", StringComparison.InvariantCultureIgnoreCase)
					|| DescFileRec.DepotFile.Contains("/Version.h", StringComparison.InvariantCultureIgnoreCase))
				{
					Logger.LogError("In CL {Change}, the file below affects asset versioning,\ntherefore it isn't safe to back it out.\n\n{FileName}", Change, DescFileRec.DepotFile);
					Result.Success = false;
					Result.Reason = $"In CL {Change}, the file below affects asset versioning, therefore it isn't safe to back it out.\n\n{DescFileRec.DepotFile}";
					return Result;

				}

				// if it's a binary file, are we backing out the head revision?
				if (DescFileRec.Type.Contains("+l", StringComparison.InvariantCultureIgnoreCase))
				{
					// check the latest revision of this depot file
					FStatRecord StatRecord = (await Perforce.FStatAsync(new List<string> { DescFileRec.DepotFile }).ToListAsync()).First();
					if (StatRecord.HeadRevision != DescFileRec.Revision)
					{
						Logger.LogError("\nIn CL {Change}, the file below is a binary file and revision #{Rev} isn't the head revision (#{HeadRev}),\ntherefore it isn't safe to back it out\n\n{FileName}",
							Change, DescFileRec.Revision, StatRecord.HeadRevision, DescFileRec.DepotFile);
						Result.Success = false;
						Result.Reason = $"\nIn CL {Change}, the file below is a binary file and revision #{DescFileRec.Revision} isn't the head revision (#{StatRecord.HeadRevision}), therefore it isn't safe to back it out\n\n{DescFileRec.DepotFile}";
						return Result;
					}
				}
			}

			Logger.LogInformation("CL {Change} seems safe to backout", Change);
			return Result;
		}


		
		public override async Task<int> Execute(string[] Args, IReadOnlyDictionary<string, string> ConfigValues, ILogger Logger)
		{
			int Change;
			if (Args.Length < 2)
			{
				Logger.LogError("Missing changelist number");
				return 1;
			}
			else if (!int.TryParse(Args[1], out Change))
			{
				Logger.LogError("'{Argument}' is not a numbered changelist", Args[1]);
				return 1;
			}

			bool Debug = Args.Any(x => x.Equals("-Debug", StringComparison.OrdinalIgnoreCase));

			using PerforceConnection Perforce = new PerforceConnection(null, null, Logger);

			BackoutValidationResult ValidationResult = await ValidateBackoutSafety(Perforce, Change, Logger);

			if (ValidationResult.Success == false)
			{
				Logger.LogInformation("\r\nCL {Change} isn't safe to backout, please confirm if you want to proceed.\r\n", Change);

				// warn user
				MessageBoxResult result = MessageBox.Show(
					"This backout is potentially unsafe:\r\n" +
					"\r\n" +
					ValidationResult.Reason +
					"\r\n\r\n" +
					"Are you sure you want to proceed with the backout operation?\r\n" +
					"\r\n" +
					"You can tag @p4backouthelp in #ue-build-health or #fn-build-health for assistance.\r\n" +
					"\r\n",
					"Unsafe backout detected",
					MessageBoxButton.YesNo);

				if (result == MessageBoxResult.No)
				{
					Logger.LogInformation("Operation canceled.");
					return 0;
				}
				else
				{
					Logger.LogInformation("Proceeding with backout operation.");
				}
			}
			
			DescribeRecord ExistingChangeRecord = await Perforce.DescribeAsync(Change, CancellationToken.None);

			// P4V undo checks for files opened before executing 'undo'
			List<OpenedRecord> OpenedRecords = await Perforce.OpenedAsync(OpenedOptions.AllWorkspaces | OpenedOptions.ShortOutput, ExistingChangeRecord.Files.Select(x => x.DepotFile).ToArray(), CancellationToken.None).ToListAsync();
			if (OpenedRecords.Count > 0)
			{
				string FileList = String.Join("\r\n", OpenedRecords.Select(x => x.DepotFile));

				Logger.LogInformation("\r\nSome files are checked out on another workspace, please confirm that you wish to continue.\r\n");

				// prompt
				MessageBoxResult result = MessageBox.Show(
					"The following files are checked out on another workspace:\r\n" +
					"\r\n" +
					FileList +
					"\r\n\r\n" +
					"Do you want to proceed with the backout operation?\r\n" +
					"\r\n",
					"Warning",
					MessageBoxButton.YesNo);

				if (result == MessageBoxResult.No)
				{
					Logger.LogInformation("Operation canceled.");
					return 0;
				}
				else
				{
					Logger.LogInformation("Proceeding with backout operation.");
				}
			}

			InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.None, CancellationToken.None);

			// Create a new CL
			ChangeRecord NewChangeRecord = new ChangeRecord();
			NewChangeRecord.User = Info.UserName;
			NewChangeRecord.Client = Info.ClientName;
			NewChangeRecord.Description = $"[Backout] - CL{Change}\n#fyi {ExistingChangeRecord.User}\nOriginal CL Desc\n-----------------------------------------------------------------\n{ExistingChangeRecord.Description.TrimEnd()}\n";
			NewChangeRecord = await Perforce.CreateChangeAsync(NewChangeRecord, CancellationToken.None);

			Logger.LogInformation("Created pending changelist {Change}", NewChangeRecord.Number);

			// Undo the passed in CL
			PerforceResponseList<UndoRecord> UndoResponses = await Perforce.TryUndoChangeAsync(Change, NewChangeRecord.Number, CancellationToken.None);

			// Grab new CL info
			DescribeRecord RefreshNewRecord = await Perforce.DescribeAsync(NewChangeRecord.Number, CancellationToken.None);

			// if the original CL and the new CL differ in file count then an error occurs, abort and clean up
			if (RefreshNewRecord.Files.Count != ExistingChangeRecord.Files.Count)
			{
				Logger.LogError("Undo on CL {Change} failed (probably due to an exclusive check out)", Change);
				foreach (PerforceResponse Response in UndoResponses)
				{
					Logger.LogError("  {Response}", Response.ToString());
				}

				// revert files in the new CL
				List<OpenedRecord> NewCLOpenedRecords = await Perforce.OpenedAsync(OpenedOptions.None, NewChangeRecord.Number, null, null, -1, FileSpecList.Any, CancellationToken.None).ToListAsync();

				if (OpenedRecords.Count > 0)
				{
					Logger.LogError("  Reverting");
					await Perforce.RevertAsync(NewChangeRecord.Number, null, RevertOptions.DeleteAddedFiles, NewCLOpenedRecords.Select(x => x.ClientFile!).ToArray(), CancellationToken.None);
				}

				// delete the new CL
				Logger.LogError("  Deleting newly created CL {Change}", NewChangeRecord.Number);
				await Perforce.DeleteChangeAsync(DeleteChangeOptions.None, RefreshNewRecord.Number, CancellationToken.None);

				return 1;
			}
			else
			{
				Logger.LogInformation("Undo of {Change} created CL {NewChange}", Change, NewChangeRecord.Number);
			}

			// Convert the undo CL over to an edit.
			if(!await ConvertToEditCommand.ConvertToEditAsync(Perforce, NewChangeRecord.Number, Debug, Logger))
			{
				return 1;
			}

			return 0;
		}

	}

}
