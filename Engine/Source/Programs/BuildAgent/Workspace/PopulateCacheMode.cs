// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace
{
	[ProgramMode("PopulateCache", "Populates the cache with the head revision of the given streams")]
	class PopulateCacheMode : WorkspaceMode
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

		protected override void Execute(Repository Repo)
		{
			List<string> ExpandedFilters = ExpandFilters(Filters);

			List<KeyValuePair<string, string>> ClientAndStreams = ParseClientAndStreams(ClientAndStreamParams);
			Repo.Populate(ClientAndStreams, Filters, bFakeSync);
		}
	}
}
