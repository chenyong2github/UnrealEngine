// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands.Vcs
{
	[Command("vcs", "branch", "Switch to a new branch")]
	class VcsBranchCommand : VcsBase
	{
		[CommandLine(Prefix = "-Name=", Required = true)]
		public string Name { get; set; } = "";

		public VcsBranchCommand(IOptions<AgentSettings> settings)
			: base(settings)
		{
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			DirectoryReference rootDir = FindRootDir();

			WorkspaceState workspaceState = await ReadStateAsync(rootDir);

			using IStorageClientOwner owner = CreateStorageClient(rootDir, logger);
			IStorageClient store = owner.Store;

			RefName branchName = new RefName(Name);
			if (await store.HasRefAsync(branchName))
			{
				logger.LogError("Branch {BranchName} already exists - use checkout instead.", branchName);
				return 1;
			}

			logger.LogInformation("Starting work in new branch {BranchName}", branchName);

			workspaceState.Branch = new RefName(Name);
			workspaceState.Tree = new DirectoryState();
			await WriteStateAsync(rootDir, workspaceState);

			return 0;
		}
	}
}
