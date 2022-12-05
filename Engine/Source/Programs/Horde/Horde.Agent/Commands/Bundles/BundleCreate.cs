// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		protected interface IStorageClientOwner : IDisposable
		{
			public IMemoryCache Cache { get; }
			public IStorageClient Store { get; }
		}

		class FileStorageClientOwner : IStorageClientOwner
		{
			public IMemoryCache Cache { get; }
			public IStorageClient Store { get; }

			public FileStorageClientOwner(DirectoryReference storageDir, ILogger logger)
			{
				Cache = new MemoryCache(new MemoryCacheOptions());
				Store = new FileStorageClient(storageDir, logger);
			}

			public void Dispose()
			{
				Cache.Dispose();
			}
		}

		class HttpStorageClientOwner : IStorageClientOwner
		{
			public IMemoryCache Cache { get; }
			public IStorageClient Store { get; }
			readonly IOptions<AgentSettings> _settings;
			readonly NamespaceId _namespaceId;
		
			public HttpStorageClientOwner(IOptions<AgentSettings> settings, NamespaceId namespaceId, ILogger logger)
			{
				Cache = new MemoryCache(new MemoryCacheOptions());
				Store = new HttpStorageClient(CreateDefaultClient, () => new HttpClient(), logger);
				_settings = settings;
				_namespaceId = namespaceId;
			}

			HttpClient CreateDefaultClient()
			{
				ServerProfile profile = _settings.Value.GetCurrentServerProfile();

				HttpClient client = new HttpClient();
				client.BaseAddress = new Uri(profile.Url, $"api/v1/storage/{_namespaceId}/");
				if (!String.IsNullOrEmpty(profile.Token))
				{
					client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", profile.Token);
				}

				return client;
			}

			public void Dispose()
			{
				Cache.Dispose();
			}
		}

		public static RefName DefaultRefName { get; } = new RefName("default-ref");

		[CommandLine("-Http")]
		public bool Http { get; set; }

		[CommandLine("-Namespace=", Description = "Namespace to use for storage")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default");

		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference StorageDir { get; set; } = DirectoryReference.Combine(Program.AppDir, "bundles");

		readonly IOptions<AgentSettings> _settings;

		public BundleCommandBase(IOptions<AgentSettings> settings)
		{
			_settings = settings;
		}

		protected IStorageClientOwner CreateStorageClient(ILogger logger)
		{
			if (Http)
			{
				return new HttpStorageClientOwner(_settings, NamespaceId, logger);
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

		public CreateCommand(IOptions<AgentSettings> settings)
			: base(settings)
		{
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using IStorageClientOwner storeOwner = CreateStorageClient(logger);
			IStorageClient store = storeOwner.Store;

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
