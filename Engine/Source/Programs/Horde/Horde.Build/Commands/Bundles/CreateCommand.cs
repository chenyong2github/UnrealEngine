// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Horde.Build.Perforce;
using Horde.Build.Storage;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Bundles
{
	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class CreateCommand : Command
	{
		[CommandLine("-Ref=")]
		public RefName RefName { get; set; } = new RefName("default-ref");

		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = Namespace.Artifacts;

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		readonly IConfiguration _configuration;
		readonly ILoggerProvider _loggerProvider;

		public CreateCommand(IConfiguration configuration, ILoggerProvider loggerProvider)
		{
			_configuration = configuration;
			_loggerProvider = loggerProvider;
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using ServiceProvider serviceProvider = Startup.CreateServiceProvider(_configuration, _loggerProvider);

			StorageService storageService = serviceProvider.GetRequiredService<StorageService>();
			IStorageClient storageClient = await storageService.GetClientAsync(NamespaceId, CancellationToken.None);			
			
			using TreeWriter writer = new TreeWriter(storageClient, prefix: RefName.Text);

			DirectoryNode node = new DirectoryNode(DirectoryFlags.None);
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), writer, new CopyStatsLogger(logger), CancellationToken.None);

			await writer.WriteAsync(RefName, node);
			return 0;
		}
	}
}
