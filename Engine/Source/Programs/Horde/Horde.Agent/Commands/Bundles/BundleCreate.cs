// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class BundleCreate : Command
	{
		[CommandLine("-Ref=")]
		public RefName RefName { get; set; } = new RefName("default-bundle");

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			FileStorageClient store = new FileStorageClient(DirectoryReference.Combine(Program.DataDir, "Bundles"), logger);

			TreeWriter writer = new TreeWriter(store, prefix: RefName.Text);

			DirectoryNode node = new DirectoryNode(DirectoryFlags.None);

			Stopwatch timer = Stopwatch.StartNew();

			ChunkingOptions options = new ChunkingOptions();
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), options, writer, CancellationToken.None);

			await writer.WriteAsync(RefName, node);

			logger.LogInformation("Time: {Time}", timer.Elapsed.TotalSeconds);
			return 0;
		}
	}
}
