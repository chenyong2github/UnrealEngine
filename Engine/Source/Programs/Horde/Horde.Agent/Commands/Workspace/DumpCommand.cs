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
	[Command("Workspace", "Dump", "Dumps the contents of the repository to the log for analysis")]
	class Dump : WorkspaceCommand
	{
		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			Repo.Dump();
			return Task.CompletedTask;
		}
	}
}
