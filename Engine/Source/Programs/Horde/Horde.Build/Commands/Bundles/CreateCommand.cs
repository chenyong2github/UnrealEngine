// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Bundles;
using EpicGames.Horde.Storage.Nodes;
using Horde.Build.Perforce;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Bundles
{
	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class CreateCommand : Command
	{
		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = new RefId("default-ref");

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		protected readonly IConfiguration _configuration;
		protected readonly ILoggerProvider _loggerProvider;

		public CreateCommand(IConfiguration configuration, ILoggerProvider loggerProvider)
		{
			_configuration = configuration;
			_loggerProvider = loggerProvider;
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using ServiceProvider serviceProvider = Startup.CreateServiceProvider(_configuration, _loggerProvider);

			ITreeStore store = serviceProvider.GetRequiredService<ITreeStore<CommitService>>();

			DirectoryNode node = new DirectoryNode();
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), logger, CancellationToken.None);
			await store.WriteTreeAsync(RefId, node);

			return 0;
		}
	}
}
