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
	[Command("tree", "extract", "Extracts data from a tree to the local hard drive")]
	class ExtractCommand : Command
	{
		IConfiguration Configuration;

		[CommandLine("-Namespace=", Required = true)]
		public NamespaceId NamespaceId { get; set; }

		[CommandLine("-Bucket=", Required = true)]
		public BucketId BucketId { get; set; }

		[CommandLine("-Ref=", Required = true)]
		public RefId RefId { get; set; }

		[CommandLine("-Output=")]
		public DirectoryReference? OutputDir { get; set; }

		public ExtractCommand(IConfiguration Configuration)
		{
			this.Configuration = Configuration;
		}

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IServiceCollection Services = new ServiceCollection();
			Startup.AddServices(Services, Configuration);

			DirectoryReference StorageDir = DirectoryReference.Combine(Program.DataDir, "Storage");
			Services.AddSingleton<IStorageClient, FileStorageClient>(SP => new FileStorageClient(StorageDir, Logger));

			OutputDir ??= DirectoryReference.Combine(Program.AppDir, "Output");
			Logger.LogInformation("Writing output to {OutputDir}", OutputDir);

			IServiceProvider ServiceProvider = Services.BuildServiceProvider();
			IStorageClient StorageClient = ServiceProvider.GetRequiredService<IStorageClient>();

			IRef Ref = await StorageClient.GetRefAsync(NamespaceId, BucketId, RefId);

			TreePack Pack = new TreePack(StorageClient, NamespaceId);
			IoHash RootHash = Pack.AddRootObject(Ref).GetRootHash();

			TreePackDirNode RootNode = TreePackDirNode.Parse(await Pack.GetDataAsync(RootHash));
			await ExtractDataAsync(Pack, RootNode, OutputDir);

			return 0;
		}

		public async Task ExtractDataAsync(TreePack Pack, TreePackDirNode DirNode, DirectoryReference OutputDir)
		{
			DirectoryReference.CreateDirectory(OutputDir);
			foreach (TreePackDirEntry Entry in DirNode.Entries)
			{
				if ((Entry.Flags & TreePackDirEntryFlags.Directory) != 0)
				{
					DirectoryReference SubOutputDir = DirectoryReference.Combine(OutputDir, Entry.Name.ToString());
					TreePackDirNode SubDirNode = TreePackDirNode.Parse(await Pack.GetDataAsync(Entry.Hash));
					await ExtractDataAsync(Pack, SubDirNode, SubOutputDir);
				}
				else if ((Entry.Flags & TreePackDirEntryFlags.File) != 0)
				{
					FileReference File = FileReference.Combine(OutputDir, Entry.Name.ToString());
					ReadOnlyMemory<byte> Data = await Pack.GetDataAsync(Entry.Hash);
					await FileReference.WriteAllBytesAsync(File, Data.ToArray());
				}
			}
		}
	}
}
