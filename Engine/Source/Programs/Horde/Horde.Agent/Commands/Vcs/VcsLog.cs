// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands.Vcs
{
	[Command("Vcs", "Log", "Print a history of commits")]
	class VcsLogCommand : VcsBase
	{
		[CommandLine("-Count")]
		public int Count { get; set; } = 20;

		public VcsLogCommand(IOptions<AgentSettings> settings)
			: base(settings)
		{
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			DirectoryReference rootDir = FindRootDir();

			WorkspaceState workspaceState = await ReadStateAsync(rootDir);

			using IStorageClientOwner owner = CreateStorageClient(rootDir, logger);
			IStorageClient store = owner.Store;
			TreeReader reader = new TreeReader(owner.Store, owner.Cache, logger);

			List<CommitNode> commits = new List<CommitNode>();

			CommitNode? tip = await reader.TryReadNodeAsync<CommitNode>(workspaceState.Branch);
			if (tip != null)
			{
				for (int idx = 0; idx < Count; idx++)
				{
					commits.Add(tip);

					if (tip.Parent == null)
					{
						break;
					}

					tip = await tip.Parent.ExpandAsync(reader);
				}
			}

			foreach (CommitNode commit in commits)
			{
				logger.LogInformation("Commit {Number}: {Message}", commit.Number, commit.Message);
			}

			return 0;
		}
	}
}
