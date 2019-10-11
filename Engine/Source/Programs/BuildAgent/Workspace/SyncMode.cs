// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	[ProgramMode("Sync", "Syncs the files for a particular stream and changelist")]
	class SyncMode : WorkspaceMode
	{
		[CommandLine("-Client=", Required = true)]
		[Description("Name of the client to sync. Will be created if it does not exist.")]
		public string ClientName = null;

		[CommandLine("-Stream=", Required = true)]
		[Description("The stream to sync")]
		public string StreamName = null;

		[CommandLine("-Change=")]
		[Description("The change to sync. May be a changelist number, or 'Latest'")]
		public string Change = "Latest";

		[CommandLine]
		[Description("Optional path to a cache file used to store workspace metadata. Using a location on a network share allows multiple machines syncing the same CL to only query Perforce state once.")]
		FileReference CacheFile = null;

		[CommandLine("-Filter=")]
		[Description("Filters for the files to sync, in P4 syntax (eg. /Engine/...)")]
		public List<string> Filters = new List<string>();

		[CommandLine("-FakeSync")]
		[Description("Simulates the sync without actually fetching any files")]
		public bool bFakeSync = false;

		protected override void Execute(Repository Repo)
		{
			int ChangeNumber = ParseChangeNumberOrLatest(Change);
			List<string> ExpandedFilters = ExpandFilters(Filters);

			Repo.Sync(ClientName, StreamName, ChangeNumber, ExpandedFilters, bFakeSync, CacheFile);
		}
	}
}
