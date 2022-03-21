// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using System.Threading;
using System.Threading.Tasks;

namespace Horde.Agent.Commands.Workspace
{
	[Command("Workspace", "RepairCache", "Checks the integrity of the cache, and removes any invalid files")]
	class RepairCommand : WorkspaceCommand
	{
		protected override Task ExecuteAsync(IPerforceConnection perforce, ManagedWorkspace repo, ILogger logger)
		{
			return repo.RepairAsync(CancellationToken.None);
		}
	}
}
