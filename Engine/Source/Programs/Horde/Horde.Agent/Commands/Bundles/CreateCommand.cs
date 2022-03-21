// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Threading.Tasks;

namespace Horde.Agent.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		[CommandLine("-Server=")]
		public string? Server { get; set; } = null;

		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference? StorageDir { get; set; } = null;

		protected IStorageClient CreateStorageClient(ILogger logger)
		{
			if (StorageDir != null)
			{
				return new FileStorageClient(StorageDir, logger);
			}
			else
			{
				IConfiguration config = new ConfigurationBuilder()
					.AddJsonFile("appsettings.json", optional: false)
					.AddJsonFile($"appsettings.{Environment.GetEnvironmentVariable("DOTNET_ENVIRONMENT")}.json", optional: true) // environment variable overrides, also used in k8s setups with Helm
					.AddJsonFile("appsettings.User.json", optional: true)
					.AddEnvironmentVariables()
					.Build();

				IConfigurationSection configSection = config.GetSection(AgentSettings.SectionName);

				IServiceCollection services = new ServiceCollection();
				services.AddHordeStorage(settings => configSection.GetCurrentServerProfile().GetSection(nameof(ServerProfile.Storage)).Bind(settings));

				IServiceProvider serviceProvider = services.BuildServiceProvider();
				return serviceProvider.GetRequiredService<IStorageClient>();
			}
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

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			IStorageClient storageClient = base.CreateStorageClient(logger);
			using (MemoryCache cache = new MemoryCache(new MemoryCacheOptions { SizeLimit = 50 * 1024 * 1024 }))
			{
				using Bundle<DirectoryNode> bundle = Bundle.Create<DirectoryNode>(storageClient, NamespaceId, new BundleOptions(), cache);
				await bundle.Root.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), logger);
				await bundle.WriteAsync(BucketId, RefId, CbObject.Empty, false);
			}

			return 0;
		}
	}
}
