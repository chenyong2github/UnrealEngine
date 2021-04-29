// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using EpicGames.Core;
using EpicGames.Perforce;
using EpicGames.Perforce.Managed;

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "Sizes", "Gathers stats for which streams take the most amount of space in the cache for the given configuration")]
	class SizesCommand : WorkspaceCommand
	{
		[CommandLine("-TempClient=", Required = true)]
		[Description("Name of a temporary client to switch between streams gathering metadata. Will be created if it does not exist.")]
		string TempClientName = null!;

		[CommandLine("-Stream=")]
		[Description("Streams that should be included in the output")]
		List<string> StreamNames = new List<string>();

		[CommandLine("-Filter=")]
		[Description("Filters for the files to sync, in P4 syntax (eg. /Engine/...)")]
		List<string> Filters = new List<string>();

		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			PerforceClientConnection PerforceClient = new PerforceClientConnection(Perforce, TempClientName);
			return Repo.StatsAsync(PerforceClient, StreamNames, Filters, CancellationToken.None);
		}
	}
}
