// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference? StorageDir = null;

		protected IStorageClient CreateStorageClient(ILogger Logger)
		{
			StorageDir ??= DirectoryReference.Combine(Program.DataDir, "Storage");
			return new FileStorageClient(StorageDir, Logger);
		}
	}

	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class CreateCommand : BundleCommandBase
	{
		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default-ns");

		[CommandLine("-Bucket=")]
		public BucketId BucketId { get; set; } = new BucketId("default-bkt");

		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = new RefId("default-ref");

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IStorageClient StorageClient = base.CreateStorageClient(Logger);

			Bundle<DirectoryNode> NewBundle = Bundle.Create<DirectoryNode>(StorageClient, NamespaceId, new BundleOptions(), null);
			await NewBundle.Root.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), Logger);
			await NewBundle.WriteAsync(BucketId, RefId, false, DateTime.UtcNow);

			return 0;
		}
	}
}
