// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands.Bundles
{
	[Command("bundle", "extract", "Extracts data from a bundle to the local hard drive")]
	internal class ExtractCommand : BundleCommandBase
	{
		[CommandLine("-Ref=")]
		public RefName RefName { get; set; } = DefaultRefName;

		[CommandLine("-OutputDir=", Required = true)]
		public DirectoryReference OutputDir { get; set; } = null!;

		public ExtractCommand(IOptions<AgentSettings> settings)
			: base(settings)
		{
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using IStorageClientOwner owner = CreateStorageClient(logger);
			IStorageClient store = owner.Store;
			TreeReader reader = new TreeReader(owner.Store, owner.Cache, logger);

			Stopwatch timer = Stopwatch.StartNew();

			DirectoryNode node = await reader.ReadNodeAsync<DirectoryNode>(RefName);
			await node.CopyToDirectoryAsync(reader, OutputDir.ToDirectoryInfo(), logger, CancellationToken.None);

			logger.LogInformation("Elapsed: {Time}s", timer.Elapsed.TotalSeconds);
			return 0;
		}
	}
}
