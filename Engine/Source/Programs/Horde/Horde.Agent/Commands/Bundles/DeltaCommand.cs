// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	[Command("bundle", "delta", "Desribe the delta between two bundles")]
	internal class DeltaCommand : BundleCommandBase
	{
		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default-ns");

		[CommandLine("-Bucket=")]
		public BucketId BucketId { get; set; } = new BucketId("default-bkt");

		[CommandLine("-Ref1=", Required = true)]
		public RefId RefId1 { get; set; }

		[CommandLine("-Ref2=", Required = true)]
		public RefId RefId2 { get; set; }

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			IStorageClient storageClient = CreateStorageClient(logger);
			await BundleTools.FindDeltaAsync(storageClient, NamespaceId, BucketId, RefId1, RefId2, logger);
			return 0;
		}
	}
}
