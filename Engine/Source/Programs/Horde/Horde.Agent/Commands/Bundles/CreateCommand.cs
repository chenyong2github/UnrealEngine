// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		protected interface IStorageClientOwner : IDisposable
		{
			public IStorageClient Store { get; }
		}

		class FileStorageClientOwner : IStorageClientOwner
		{
			readonly IMemoryCache _cache;
			public IStorageClient Store { get; }

			public FileStorageClientOwner(DirectoryReference storageDir, ILogger logger)
			{
				_cache = new MemoryCache(new MemoryCacheOptions());
				Store = new FileStorageClient(storageDir, _cache, logger);
			}

			public void Dispose()
			{
				_cache.Dispose();
			}
		}

		class HttpStorageClientOwner : IStorageClientOwner
		{
			readonly IMemoryCache _cache;
			readonly HttpClient _httpClient;
			public IStorageClient Store { get; }

			public HttpStorageClientOwner(NamespaceId namespaceId, Uri baseUri, ILogger logger)
			{
				_cache = new MemoryCache(new MemoryCacheOptions());
				_httpClient = new HttpClient { BaseAddress = baseUri };
				Store = new HttpStorageClient(namespaceId, _httpClient, _cache, logger);
			}

			public void Dispose()
			{
				_httpClient.Dispose();
				_cache.Dispose();
			}
		}

		public static RefName DefaultRefName { get; } = new RefName("default-ref");

		[CommandLine("-Http")]
		public bool Http { get; set; }

		[CommandLine("-Server=", Description = "Server to read from")]
		public string Server { get; set; } = "https://localhost:5001";

		[CommandLine("-Namespace=", Description = "Namespace to use for storage")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default");

		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference StorageDir { get; set; } = DirectoryReference.Combine(Program.AppDir, "bundles");

		protected IStorageClientOwner CreateStorageClient(ILogger logger)
		{
			if (Http)
			{
				return new HttpStorageClientOwner(NamespaceId, new Uri(Server), logger);
			}
			else
			{
				return new FileStorageClientOwner(DirectoryReference.Combine(StorageDir, NamespaceId.ToString()), logger);
			}
		}
	}

	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class CreateCommand : BundleCommandBase
	{
		[CommandLine("-Ref=")]
		public RefName RefName { get; set; } = DefaultRefName;

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using IStorageClientOwner storeOwner = CreateStorageClient(logger);
			IStorageClient store = storeOwner.Store;

			TreeWriter writer = new TreeWriter(store, prefix: RefName.Text);

			DirectoryNode node = new DirectoryNode(DirectoryFlags.None);

			Stopwatch timer = Stopwatch.StartNew();

			ChunkingOptions options = new ChunkingOptions();
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), options, writer, CancellationToken.None);

			await writer.WriteRefAsync(RefName, node);

			logger.LogInformation("Time: {Time}", timer.Elapsed.TotalSeconds);
			return 0;
		}
	}
}
