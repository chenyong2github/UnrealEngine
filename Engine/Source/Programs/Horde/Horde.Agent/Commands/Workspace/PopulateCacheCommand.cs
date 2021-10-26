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
	[Command("Workspace", "PopulateWorkspaceCache", "Populates the cache with the head revision of the given streams")]
	class PopulateCacheCommand : WorkspaceCommand
	{
		[CommandLine("-ClientAndStream=")]
		[Description("Specifies client and stream pairs, in the format Client:Stream")]
		List<string> ClientAndStreamParams = new List<string>();

		[CommandLine("-Filter=")]
		[Description("Filters for the files to sync, in P4 syntax (eg. /Engine/...)")]
		List<string> Filters = new List<string>();

		[CommandLine("-FakeSync")]
		[Description("Simulates the sync without actually fetching any files")]
		bool bFakeSync = false;

		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			List<string> ExpandedFilters = ExpandFilters(Filters);

			List<PopulateRequest> PopulateRequests = new List<PopulateRequest>();
			foreach (string ClientAndStreamParam in ClientAndStreamParams)
			{
				int Idx = ClientAndStreamParam.IndexOf(':');
				if (Idx == -1)
				{
					throw new FatalErrorException("Expected -ClientAndStream=<ClientName>:<StreamName>");
				}

				PerforceClientConnection PerforceClient = new PerforceClientConnection(Perforce, ClientAndStreamParam.Substring(0, Idx));
				PopulateRequests.Add(new PopulateRequest(PerforceClient, ClientAndStreamParam.Substring(Idx + 1), ExpandedFilters));
			}

			return Repo.PopulateAsync(PopulateRequests, bFakeSync, CancellationToken.None);
		}
	}
}
