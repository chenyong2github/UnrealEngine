// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
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

		[CommandLine("-FilterFile=")]
		[Description("A file containing a list of paths to filter when syncing, in P4 syntax (eg. /Engine/...)")]
		FileReference FilterFile;

		[CommandLine("-FakeSync")]
		[Description("Simulates the sync without actually fetching any files")]
		bool bFakeSync = false;

		protected override void Execute(Repository Repo)
		{
			List<string> ExpandedFilters = ExpandFilters(Filters);
			if (FilterFile != null)
			{
				if (!FileReference.Exists(FilterFile))
				{
					throw new FileNotFoundException(string.Format("Filter file '{0}' could not be found!", FilterFile), FilterFile.FullName);
				}

				ExpandedFilters.AddRange(FileReference.ReadAllLines(FilterFile));
			}

			List<KeyValuePair<string, string>> ClientAndStreams = ParseClientAndStreams(ClientAndStreamParams);
			List<string> DistinctFilters = ExpandedFilters.Distinct(StringComparer.InvariantCultureIgnoreCase).ToList();
			Repo.Populate(ClientAndStreams, DistinctFilters, bFakeSync);
		}
	}
}
