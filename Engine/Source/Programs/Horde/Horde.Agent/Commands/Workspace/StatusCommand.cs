// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using System.Threading.Tasks;

namespace Horde.Agent.Commands.Workspace
{
	[Command("Workspace", "Status", "Prints information about the state of the cache and workspace")]
	class StatusCommand : WorkspaceCommand
	{
		protected override Task ExecuteAsync(IPerforceConnection perforce, ManagedWorkspace repo, ILogger logger)
		{
			repo.Status();
			return Task.CompletedTask;
		}
	}
}
