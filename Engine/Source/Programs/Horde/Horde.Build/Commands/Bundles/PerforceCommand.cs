// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Horde.Build.Perforce;
using Horde.Build.Storage;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Bundles
{
	using StreamId = StringId<IStream>;

	[Command("bundle", "perforce", "Replicates commits for a particular change of changes from Perforce")]
	class PerforceCommand : Command
	{
		[CommandLine("-Stream=", Required = true)]
		public string StreamId { get; set; } = String.Empty;

		[CommandLine(Required = true)]
		public int Change { get; set; }

		[CommandLine]
		public int BaseChange { get; set; }

		[CommandLine]
		public int Count { get; set; } = 1;

		[CommandLine]
		public bool Content { get; set; }

		[CommandLine]
		public bool Compact { get; set; } = true;

		[CommandLine]
		public string Filter { get; set; } = "...";

		[CommandLine]
		public bool RevisionsOnly { get; set; } = false;

		[CommandLine]
		public DirectoryReference? OutputDir { get; set; }

		readonly IConfiguration _configuration;
		readonly ILoggerProvider _loggerProvider;

		public PerforceCommand(IConfiguration configuration, ILoggerProvider loggerProvider)
		{
			_configuration = configuration;
			_loggerProvider = loggerProvider;
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using ServiceProvider serviceProvider = Startup.CreateServiceProvider(_configuration, _loggerProvider);

			ICommitService commitService = serviceProvider.GetRequiredService<ICommitService>();
			ReplicationService replicationService = serviceProvider.GetRequiredService<ReplicationService>();
			IStreamCollection streamCollection = serviceProvider.GetRequiredService<IStreamCollection>();
			StorageService storageService = serviceProvider.GetRequiredService<StorageService>();

			IStorageClient storage = await storageService.GetClientAsync(Namespace.Perforce, CancellationToken.None);

			IStream? stream = await streamCollection.GetAsync(new StreamId(StreamId));
			if (stream == null)
			{
				throw new FatalErrorException($"Stream '{stream}' not found");
			}

			Dictionary<IStream, int> streamToFirstChange = new Dictionary<IStream, int>();
			streamToFirstChange[stream] = Change;

			ReplicationNode baseContents;
			if (BaseChange == 0)
			{
				baseContents = new ReplicationNode(new DirectoryNode(DirectoryFlags.WithGitHashes));
			}
			else
			{
				baseContents = await replicationService.ReadCommitTreeAsync(storage, stream, BaseChange, Filter, RevisionsOnly, CancellationToken.None);
			}

			await foreach (ICommit commit in commitService.GetCollection(stream).FindAsync(Change, null, Count))
			{
				string briefSummary = commit.Description.Replace('\n', ' ');
				logger.LogInformation("");
				logger.LogInformation("Commit {Change} by {AuthorId}: {Summary}", commit.Number, commit.AuthorId, briefSummary.Substring(0, Math.Min(50, briefSummary.Length)));
				logger.LogInformation("Base path: {BasePath}", commit.BasePath);

				if (Content)
				{
					baseContents = await replicationService.WriteCommitTreeAsync(storage, stream, commit.Number, BaseChange, baseContents, Filter, RevisionsOnly, CancellationToken.None);
					BaseChange = commit.Number;
				}
			}

			return 0;
		}
	}
}
