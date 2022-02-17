// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using HordeServer.Collections;
using HordeServer.Commits;
using HordeServer.Commits.Impl;
using HordeServer.Controllers;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Commands
{
	using StreamId = StringId<IStream>;

	[Command("tree", "analyze", "Indexes a tree and creates a table of contents for each blob describing the contents")]
	class AnalyzeCommand : Command
	{
		IConfiguration Configuration;

		[CommandLine("-Namespace=", Required = true)]
		public NamespaceId NamespaceId { get; set; }

		[CommandLine("-Bucket=", Required = true)]
		public BucketId BucketId { get; set; }

		[CommandLine("-Ref=", Required = true)]
		public RefId RefId { get; set; }

		public AnalyzeCommand(IConfiguration Configuration)
		{
			this.Configuration = Configuration;
		}

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IServiceCollection Services = new ServiceCollection();
			Startup.AddServices(Services, Configuration);

			DirectoryReference OutputDir = DirectoryReference.Combine(Program.DataDir, "Storage");
			Logger.LogInformation("Writing output to {OutputDir}", OutputDir);

			Services.AddSingleton<IStorageClient, FileStorageClient>(SP => new FileStorageClient(OutputDir, Logger));

			IServiceProvider ServiceProvider = Services.BuildServiceProvider();
			IStorageClient StorageClient = ServiceProvider.GetRequiredService<IStorageClient>();

			IRef Ref = await StorageClient.GetRefAsync(NamespaceId, BucketId, RefId);

			TreePack Pack = new TreePack(StorageClient, NamespaceId);
			IoHash RootHash = Pack.AddRootObject(Ref).GetRootHash();
			await Pack.WriteTreeSummaryAsync(RootHash, DirectoryReference.Combine(OutputDir, NamespaceId.ToString()), Logger);

			return 0;
		}
	}
}
