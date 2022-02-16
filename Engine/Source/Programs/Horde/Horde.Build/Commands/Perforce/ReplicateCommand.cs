// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Collections;
using HordeServer.Commits;
using HordeServer.Commits.Impl;
using HordeServer.Controllers;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Commands
{
	using StreamId = StringId<IStream>;

	[Command("perforce", "replicate", "Replicates commits for a particular change of changes from Perforce")]
	class ReplicateCommand : Command
	{
		[CommandLine("-StreamId=", Required = true)]
		public List<string> StreamIds { get; set; } = new List<string>();

		[CommandLine(Required = true)]
		public int Change { get; set; }

		[CommandLine]
		public int Count { get; set; } = 1;

		IConfiguration Configuration;

		public ReplicateCommand(IConfiguration Configuration)
		{
			this.Configuration = Configuration;
		}

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IServiceProvider ServiceProvider = Startup.CreateServiceProvider(Configuration);
			CommitService CommitService = ServiceProvider.GetRequiredService<CommitService>();

			IStreamCollection StreamCollection = ServiceProvider.GetRequiredService<IStreamCollection>();

			Dictionary<IStream, int> StreamToFirstChange = new Dictionary<IStream, int>();
			foreach(string StreamId in StreamIds)
			{
				IStream? Stream = await StreamCollection.GetAsync(new StreamId(StreamId));
				if (Stream == null)
				{
					throw new FatalErrorException($"Stream '{Stream}' not found");
				}
				StreamToFirstChange[Stream] = Change;
			}

			string ClusterName = StreamToFirstChange.First().Key.ClusterName;
			await foreach (NewCommit Commit in CommitService.FindCommitsForClusterAsync(ClusterName, StreamToFirstChange).Take(Count))
			{
				string BriefSummary = Commit.Description.Replace('\n', ' ').Substring(0, 50);
				Logger.LogInformation("Commit {Change} by {AuthorId}: {Summary}", Commit.Change, Commit.AuthorId, BriefSummary);
				Logger.LogInformation(" - Base path: {BasePath}", Commit.BasePath);
			}

			return 0;
		}
	}
}
