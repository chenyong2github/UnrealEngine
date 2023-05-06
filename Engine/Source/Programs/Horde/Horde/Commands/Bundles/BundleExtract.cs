// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Commands.Bundles
{
	[Command("bundle", "extract", "Extracts data from a bundle to the local hard drive")]
	internal class BundleExtract : Command
	{
		[CommandLine("-Input=")]
		public FileReference Input { get; set; } = null!;

		[CommandLine("-BlobDir=")]
		public DirectoryReference? BlobDir { get; set; }

		[CommandLine("-OutputDir=", Required = true)]
		public DirectoryReference OutputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			string Text = await FileReference.ReadAllTextAsync(Input);
			NodeHandle handle = NodeHandle.Parse(Text);

			FileStorageClient store = new FileStorageClient(BlobDir ?? Input.Directory, logger);

			using MemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			TreeReader reader = new TreeReader(store, cache, logger);

			Stopwatch timer = Stopwatch.StartNew();

			DirectoryNode node = await reader.ReadNodeAsync<DirectoryNode>(handle.Locator);
			await node.CopyToDirectoryAsync(reader, OutputDir.ToDirectoryInfo(), logger, CancellationToken.None);

			logger.LogInformation("Elapsed: {Time}s", timer.Elapsed.TotalSeconds);
			return 0;
		}
	}
}
