// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using Horde.Build.Perforce;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands
{
	using StreamId = StringId<IStream>;

	[Command("perforce", "replicate", "Replicates commits for a particular change of changes from Perforce")]
	class ReplicateCommand : Command
	{
		[CommandLine("-Stream=", Required = true)]
		public string StreamId { get; set; } = String.Empty;

		[CommandLine(Required = true)]
		public int Change { get; set; }

		[CommandLine]
		public int? BaseChange { get; set; }

		[CommandLine]
		public int Count { get; set; } = 1;

		[CommandLine]
		public bool Content { get; set; }

		[CommandLine]
		public bool Compact { get; set; } = true;

		[CommandLine]
		public bool Clean { get; set; }

		[CommandLine]
		public string Filter { get; set; } = "...";

		[CommandLine]
		public bool Metadata { get; set; } = false;

		[CommandLine]
		public DirectoryReference? OutputDir { get; set; }

		readonly IConfiguration _configuration;
		readonly ILoggerProvider _loggerProvider;

		public ReplicateCommand(IConfiguration configuration, ILoggerProvider loggerProvider)
		{
			_configuration = configuration;
			_loggerProvider = loggerProvider;
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			IServiceCollection services = new ServiceCollection();
			services.AddLogging(builder => builder.AddProvider(_loggerProvider));

			Startup.AddServices(services, _configuration);

			OutputDir ??= DirectoryReference.Combine(Program.DataDir, "Storage");
			logger.LogInformation("Writing output to {OutputDir}", OutputDir);
			services.AddSingleton<IStorageClient, FileStorageClient>(sp => new FileStorageClient(OutputDir, logger));

			IServiceProvider serviceProvider = services.BuildServiceProvider();
			CommitService commitService = serviceProvider.GetRequiredService<CommitService>();
			ICommitCollection commitCollection = serviceProvider.GetRequiredService<ICommitCollection>();
			IStreamCollection streamCollection = serviceProvider.GetRequiredService<IStreamCollection>();
			IStorageClient storageClient = serviceProvider.GetRequiredService<IStorageClient>();

			IStream? stream = await streamCollection.GetAsync(new StreamId(StreamId));
			if (stream == null)
			{
				throw new FatalErrorException($"Stream '{stream}' not found");
			}

			Dictionary<IStream, int> streamToFirstChange = new Dictionary<IStream, int>();
			streamToFirstChange[stream] = Change;

			await foreach (NewCommit newCommit in commitService.FindCommitsForClusterAsync(stream.ClusterName, streamToFirstChange).Take(Count))
			{
				string briefSummary = newCommit.Description.Replace('\n', ' ');
				logger.LogInformation("Commit {Change} by {AuthorId}: {Summary}", newCommit.Change, newCommit.AuthorId, briefSummary.Substring(0, Math.Min(50, briefSummary.Length)));
				logger.LogInformation(" - Base path: {BasePath}", newCommit.BasePath);

				if (Content)
				{
					if (Clean || BaseChange != null)
					{
						await commitService.WriteCommitTreeAsync(stream, newCommit.Change, BaseChange, Filter, Metadata);
					}
					BaseChange = newCommit.Change;
				}
			}

			return 0;
		}
	}
}
