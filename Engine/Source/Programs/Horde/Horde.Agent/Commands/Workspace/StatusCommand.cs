// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "Status", "Prints information about the state of the cache and workspace")]
	class StatusCommand : WorkspaceCommand
	{
		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			Repo.Status();
			return Task.CompletedTask;
		}
	}
}
