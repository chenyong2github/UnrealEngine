// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	[Command("bundle", "dump", "Indexes a tree and creates a table of contents for each blob describing the contents")]
	internal class DumpCommand : BundleCommandBase
	{
		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default-ns");

		[CommandLine("-Bucket=")]
		public BucketId BucketId { get; set; } = new BucketId("default-bkt");

		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = new RefId("default-ref");

		[CommandLine("-OutputDir=")]
		public DirectoryReference? OutputDir { get; set; }

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			OutputDir = DirectoryReference.Combine(Program.DataDir, "Bundles");

			IStorageClient storageClient = CreateStorageClient(logger);
			await BundleTools.WriteSummaryAsync(storageClient, NamespaceId, BucketId, RefId, OutputDir);

			return 0;
		}
	}
}
