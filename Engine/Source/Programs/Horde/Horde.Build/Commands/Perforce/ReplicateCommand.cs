// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
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
		public string Filter { get; set; } = "//...";

		[CommandLine]
		public DirectoryReference? OutputDir { get; set; }

		IConfiguration Configuration;
		ILoggerProvider LoggerProvider;

		public ReplicateCommand(IConfiguration Configuration, ILoggerProvider LoggerProvider)
		{
			this.Configuration = Configuration;
			this.LoggerProvider = LoggerProvider;
		}

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IServiceCollection Services = new ServiceCollection();
			Services.AddLogging(Builder => Builder.AddProvider(LoggerProvider));

			Startup.AddServices(Services, Configuration);

			OutputDir ??= DirectoryReference.Combine(Program.DataDir, "Storage");
			Logger.LogInformation("Writing output to {OutputDir}", OutputDir);
			Services.AddSingleton<IStorageClient, FileStorageClient>(SP => new FileStorageClient(OutputDir, Logger));

			IServiceProvider ServiceProvider = Services.BuildServiceProvider();
			CommitService CommitService = ServiceProvider.GetRequiredService<CommitService>();
			ICommitCollection CommitCollection = ServiceProvider.GetRequiredService<ICommitCollection>();
			IStreamCollection StreamCollection = ServiceProvider.GetRequiredService<IStreamCollection>();
			IStorageClient StorageClient = ServiceProvider.GetRequiredService<IStorageClient>();

			IStream? Stream = await StreamCollection.GetAsync(new StreamId(StreamId));
			if (Stream == null)
			{
				throw new FatalErrorException($"Stream '{Stream}' not found");
			}

			Dictionary<IStream, int> StreamToFirstChange = new Dictionary<IStream, int>();
			StreamToFirstChange[Stream] = Change;

			CommitTree? BaseTree = null; 
			if (BaseChange != 0)
			{
				ICommit? Commit = await CommitCollection.GetCommitAsync(Stream.Id, BaseChange);
				if (Commit == null)
				{
					throw new InvalidOperationException($"Unable to find existing commit for {StreamId} @ CL {BaseChange}");
				}
				if (Commit.TreeRefId == null)
				{
					throw new InvalidOperationException($"No tree for commit {StreamId} @ CL {BaseChange}");
				}
				BaseTree = new CommitTree(Commit.StreamId, Commit.Change, Commit.TreeRefId.Value);
			}

			await foreach (NewCommit NewCommit in CommitService.FindCommitsForClusterAsync(Stream.ClusterName, StreamToFirstChange).Take(Count))
			{
				string BriefSummary = NewCommit.Description.Replace('\n', ' ').Substring(0, 50);
				Logger.LogInformation("Commit {Change} by {AuthorId}: {Summary}", NewCommit.Change, NewCommit.AuthorId, BriefSummary);
				Logger.LogInformation(" - Base path: {BasePath}", NewCommit.BasePath);

				if (Content)
				{
					BaseTree = await CommitService.FindCommitTreeAsync(Stream, NewCommit.Change, BaseTree, Filter);
				}
			}

			return 0;
		}
	}
}
