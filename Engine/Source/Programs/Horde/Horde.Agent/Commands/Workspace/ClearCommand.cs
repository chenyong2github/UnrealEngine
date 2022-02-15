// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "Clear", "Empties the staging directory of any files, returning them to the cache")]
	class ClearCommand : WorkspaceCommand
	{
		protected override Task ExecuteAsync(IPerforceConnection Perforce, ManagedWorkspace Repo, ILogger Logger)
		{
			return Repo.ClearAsync(CancellationToken.None);
		}
	}
}
