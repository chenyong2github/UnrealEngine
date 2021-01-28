using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace P4VUtils.Commands
{
	class MergeCommand : Command
	{
		public override string Description => "Merge a changelist into the current stream";

		public override CustomToolInfo CustomTool => new CustomToolInfo("Merge to current stream", "%p") { ShowConsole = true };

		public override async Task<int> Execute(string[] Args, IReadOnlyDictionary<string, string> ConfigValues, ILogger Logger)
		{
			int Change = int.Parse(Args[1]);

			PerforceConnection Perforce = new PerforceConnection(null, null, null, Logger);

			InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.None, CancellationToken.None);
			if (Info.ClientStream == null)
			{
				Logger.LogError("Not currently in a stream workspace.");
				return 1;
			}

			StreamRecord TargetStream = await Perforce.GetStreamAsync(Info.ClientStream, false, CancellationToken.None);
			while (TargetStream.Type == "virtual" && TargetStream.Parent != null)
			{
				TargetStream = await Perforce.GetStreamAsync(TargetStream.Parent, false, CancellationToken.None);
			}

			DescribeRecord ExistingChangeRecord = await Perforce.DescribeAsync(Change, CancellationToken.None);
			if (ExistingChangeRecord.Files.Count == 0)
			{
				Logger.LogError("No files in selected changelist");
				return 1;
			}

			ChangeRecord NewChangeRecord = new ChangeRecord();
			NewChangeRecord.User = Info.UserName;
			NewChangeRecord.Client = Info.ClientName;
			NewChangeRecord.Description = $"{ExistingChangeRecord.Description.TrimEnd()}\n#p4v-cherrypick {Change}";
			NewChangeRecord = await Perforce.CreateChangeAsync(NewChangeRecord, CancellationToken.None);

			string SourceFileSpec = Regex.Replace(ExistingChangeRecord.Files[0].DepotFile, @"^(//[^/]+/[^/]+)/.*$", "$1/...");
			string TargetFileSpec = $"{TargetStream.Stream}/...";
			await Perforce.MergeAsync(MergeOptions.None, NewChangeRecord.Number, -1, $"{SourceFileSpec}@={Change}", TargetFileSpec, CancellationToken.None);

			Logger.LogInformation("Merged into pending changelist {0}", NewChangeRecord.Number);

			await Perforce.TryResolveAsync(NewChangeRecord.Number, ResolveOptions.Automatic, null, CancellationToken.None);
			return 0;
		}
	}
}
