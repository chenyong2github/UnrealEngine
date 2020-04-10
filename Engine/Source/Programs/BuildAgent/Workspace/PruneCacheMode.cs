// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using BuildAgent.Workspace.Common;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace
{
	[ProgramMode("PruneCache", "Attempts to remove dead data out of the cache based on existing contents files generated from populating or syncing the cache.")]
	class PruneCacheMode : WorkspaceMode
	{
		[CommandLine("-ClientAndStream=")]
		[Description("Specifies client and stream pairs, in the format Client:Stream, to use when determining what to prune.")]
		List<string> ClientAndStreamParams = new List<string>();

		protected override void Execute(Repository Repo)
		{
			Stopwatch Timer = Stopwatch.StartNew();
			IEnumerable<string> ClientNames = ParseClientAndStreams(ClientAndStreamParams).Select(ClientAndStream => ClientAndStream.Key).Distinct();
			Repo.RemoveDeadFilesFromCache(ClientNames);
			Log.TraceInformation("Pruning took {0}s", Timer.Elapsed.TotalSeconds);
		}
	}
}
