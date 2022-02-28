// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeAgent.Commands.Bundles
{
	[Command("bundle", "delta", "Desribe the delta between two bundles")]
	class DeltaCommand : BundleCommandBase
	{
		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default-ns");

		[CommandLine("-Bucket=")]
		public BucketId BucketId { get; set; } = new BucketId("default-bkt");

		[CommandLine("-Ref1=", Required = true)]
		public RefId RefId1 { get; set; }

		[CommandLine("-Ref2=", Required = true)]
		public RefId RefId2 { get; set; }

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IStorageClient StorageClient = CreateStorageClient(Logger);
			await BundleTools.FindDeltaAsync(StorageClient, NamespaceId, BucketId, RefId1, RefId2, Logger);
			return 0;
		}
	}
}
