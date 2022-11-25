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
using Microsoft.Extensions.Caching.Memory;
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
			TreeReader reader = new TreeReader(storage, serviceProvider.GetRequiredService<IMemoryCache>(), serviceProvider.GetRequiredService<ILogger<PerforceCommand>>());

			IStream? stream = await streamCollection.GetAsync(new StreamId(StreamId));
			if (stream == null)
			{
				throw new FatalErrorException($"Stream '{stream}' not found");
			}

			throw new NotImplementedException();
		}
	}
}
