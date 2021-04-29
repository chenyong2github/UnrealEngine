// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "PurgeCache", "Shrink the size of the cache to the given size")]
	class PurgeCacheCommand : WorkspaceCommand
	{
		[CommandLine("-Size=")]
		string? SizeParam = null;

		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			long Size = 0;
			if (SizeParam != null)
			{
				Size = ParseSize(SizeParam);
			}

			return Repo.PurgeAsync(Size, CancellationToken.None);
		}
	}
}
