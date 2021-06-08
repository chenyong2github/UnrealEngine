// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace P4VUtils.Commands
{
	class BackoutCommand : Command
	{
		public override string Description => "P4 Admin sanctioned method of backing out a CL";

		public override CustomToolInfo CustomTool => new CustomToolInfo("Backout this CL", "%S") { ShowConsole = true };

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

			PerforceConnection Perforce = new PerforceConnection(null, null, Logger);

			// Create a new CL
			DescribeRecord ExistingChangeRecord = await Perforce.DescribeAsync(Change, CancellationToken.None);

			InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.None, CancellationToken.None);

			ChangeRecord NewChangeRecord = new ChangeRecord();
			NewChangeRecord.User = Info.UserName;
			NewChangeRecord.Client = Info.ClientName;
			NewChangeRecord.Description = $"[Backout] - CL{Change}\n#fyi {ExistingChangeRecord.User}\nOriginal CL Desc\n-----------------------------------------------------------------\n{ExistingChangeRecord.Description.TrimEnd()}\n";
			NewChangeRecord = await Perforce.CreateChangeAsync(NewChangeRecord, CancellationToken.None);
			
			Logger.LogInformation("Created pending changelist {0}", NewChangeRecord.Number);

			// Undo the passed in CL
			PerforceResponseList<UndoRecord> UndoResponses = await Perforce.TryUndoChangeAsync(Change, NewChangeRecord.Number, CancellationToken.None);

			// Grab new CL info
			DescribeRecord RefreshNewRecord = await Perforce.DescribeAsync(NewChangeRecord.Number, CancellationToken.None);

			// if the original CL and the new CL differ in file count then an error occurs, abort and clean up
			if (RefreshNewRecord.Files.Count != ExistingChangeRecord.Files.Count)
			{
				Logger.LogError("Undo on CL {0} failed", Change);
				foreach (PerforceResponse Response in UndoResponses)
				{
					Logger.LogError("  {0}", Response.ToString())	;
				}

				// revert files in the new CL
				if (RefreshNewRecord.Files.Count > 0)
				{
					Logger.LogError("  Reverting");
					await Perforce.RevertAsync(RefreshNewRecord.Number, Info.ClientName, RevertOptions.None, new[] { "//..." }, CancellationToken.None);
				}

				// delete the new CL
				Logger.LogError("  Deleting newly created CL {0}", NewChangeRecord.Number);
				await Perforce.DeleteChangeAsync(DeleteChangeOptions.None, RefreshNewRecord.Number, CancellationToken.None);

				return 1;
			}
			else
			{
				Logger.LogInformation("Undo of {0} created CL {1}", Change, NewChangeRecord.Number);
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
