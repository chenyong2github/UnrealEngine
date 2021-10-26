// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "Sync", "Syncs the files for a particular stream and changelist")]
	class SyncCommand : WorkspaceCommand
	{
		[CommandLine("-Client=", Required = true)]
		[Description("Name of the client to sync. Will be created if it does not exist.")]
		public string ClientName = null!;

		[CommandLine("-Stream=", Required = true)]
		[Description("The stream to sync")]
		public string StreamName = null!;

		[CommandLine("-Change=")]
		[Description("The change to sync. May be a changelist number, or 'Latest'")]
		public string Change = "Latest";

		[CommandLine("-Preflight=")]
		[Description("The change to unshelve into the workspace")]
		public int PreflightChange = -1;

		[CommandLine]
		[Description("Optional path to a cache file used to store workspace metadata. Using a location on a network share allows multiple machines syncing the same CL to only query Perforce state once.")]
		FileReference? CacheFile = null;

		[CommandLine("-Filter=")]
		[Description("Filters for the files to sync, in P4 syntax (eg. /Engine/...)")]
		public List<string> Filters = new List<string>();

		[CommandLine("-Incremental")]
		[Description("Performs an incremental sync, without removing intermediates")]
		public bool bIncrementalSync = false;

		[CommandLine("-FakeSync")]
		[Description("Simulates the sync without actually fetching any files")]
		public bool bFakeSync = false;

		protected override async Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			int ChangeNumber = ParseChangeNumberOrLatest(Change);
			List<string> ExpandedFilters = ExpandFilters(Filters);

			PerforceClientConnection PerforceClient = new PerforceClientConnection(Perforce, ClientName);
			await Repo.SyncAsync(PerforceClient, StreamName, ChangeNumber, ExpandedFilters, !bIncrementalSync, bFakeSync, CacheFile, CancellationToken.None);

			if (PreflightChange != -1)
			{
				await Repo.UnshelveAsync(PerforceClient, StreamName, PreflightChange, CancellationToken.None);
			}
		}
	}
}
