// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	[Command("bundle", "extract", "Extracts data from a bundle to the local hard drive")]
	internal class ExtractCommand : BundleCommandBase
	{
		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = DefaultRefId;

		[CommandLine("-OutputDir=", Required = true)]
		public DirectoryReference OutputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			using ITreeStore<DirectoryNode> store = CreateTreeStore<DirectoryNode>(logger, cache);

			DirectoryNode node = await store.ReadTreeAsync(RefId);
			await node.CopyToDirectoryAsync(OutputDir.ToDirectoryInfo(), logger, CancellationToken.None);

			return 0;
		}
	}
}
