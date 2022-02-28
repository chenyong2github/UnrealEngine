// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
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
	[Command("bundle", "dump", "Indexes a tree and creates a table of contents for each blob describing the contents")]
	class DumpCommand : BundleCommandBase
	{
		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default-ns");

		[CommandLine("-Bucket=")]
		public BucketId BucketId { get; set; } = new BucketId("default-bkt");

		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = new RefId("default-ref");

		[CommandLine("-OutputDir=")]
		public DirectoryReference? OutputDir { get; set; }

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			OutputDir = DirectoryReference.Combine(Program.DataDir, "Bundles");

			IStorageClient StorageClient = CreateStorageClient(Logger);
			await BundleTools.WriteSummaryAsync(StorageClient, NamespaceId, BucketId, RefId, OutputDir);

			return 0;
		}
	}
}
