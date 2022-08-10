// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Bundles;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		class FileBlobStore : IBlobStore
		{
			readonly DirectoryReference _rootDir;
			readonly ILogger _logger;

			public FileBlobStore(DirectoryReference rootDir, ILogger logger)
			{
				_rootDir = rootDir;
				_logger = logger;

				DirectoryReference.CreateDirectory(_rootDir);
			}

			FileReference GetRefFile(RefName name) => FileReference.Combine(_rootDir, name.ToString() + ".ref");
			FileReference GetBlobFile(BlobId id) => FileReference.Combine(_rootDir, id.Inner.ToString() + ".blob");

			static async Task WriteAsync(FileReference file, ReadOnlySequence<byte> sequence, CancellationToken cancellationToken)
			{
				using (FileStream stream = FileReference.Open(file, FileMode.Create, FileAccess.Write, FileShare.ReadWrite))
				{
					foreach (ReadOnlyMemory<byte> memory in sequence)
					{
						await stream.WriteAsync(memory, cancellationToken);
					}
				}
			}

			#region Blobs

			public async Task<IBlob?> TryReadBlobAsync(BlobId id, CancellationToken cancellationToken = default)
			{
				FileReference file = GetBlobFile(id);
				if(!FileReference.Exists(file))
				{
					return null;
				}

				_logger.LogInformation("Reading {File}", file);
				byte[] bytes = await FileReference.ReadAllBytesAsync(file, cancellationToken);
				return BlobUtils.Deserialize(bytes);
			}

			public async Task<BlobId> WriteBlobAsync(RefName refName, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
			{
				BlobId id = new BlobId($"{refName}/{Guid.NewGuid()}");
				FileReference file = GetBlobFile(id);
				DirectoryReference.CreateDirectory(file.Directory);
				_logger.LogInformation("Writing {File}", file);
				await WriteAsync(file, BlobUtils.Serialize(data, references), cancellationToken);
				return id;
			}

			#endregion

			#region Refs

			public Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
			{
				throw new NotImplementedException();
			}

			public Task<bool> HasRefAsync(RefName name, CancellationToken cancellationToken = default)
			{
				throw new NotImplementedException();
			}

			public async Task<IBlob?> TryReadRefAsync(RefName name, CancellationToken cancellationToken = default)
			{
				FileReference file = GetRefFile(name);
				if(!FileReference.Exists(file))
				{
					return null;
				}

				_logger.LogInformation("Reading {File}", file);
				byte[] bytes = await FileReference.ReadAllBytesAsync(file, cancellationToken);
				return BlobUtils.Deserialize(bytes);
			}

			public async Task WriteRefAsync(RefName name, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
			{
				FileReference file = GetRefFile(name);
				DirectoryReference.CreateDirectory(file.Directory);
				_logger.LogInformation("Writing {File}", file);
				await WriteAsync(file, BlobUtils.Serialize(data, references), cancellationToken);
			}

			#endregion
		}

		public static RefName DefaultRefName { get; } = new RefName("default-ref");

		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference StorageDir { get; set; } = DirectoryReference.Combine(Program.AppDir, "bundles");

		IBlobStore? _blobStore;

		protected IBlobStore CreateBlobStore(ILogger logger)
		{
			_blobStore ??= new FileBlobStore(StorageDir, logger);
			return _blobStore;
		}

		protected ITreeStore CreateTreeStore(ILogger logger, IMemoryCache cache)
		{
			IBlobStore blobStore = CreateBlobStore(logger);
			return new BundleStore(blobStore, new BundleOptions(), cache);
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
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			using ITreeStore store = CreateTreeStore(logger, cache);
			ITreeWriter writer = store.CreateTreeWriter(RefName);

			DirectoryNode node = new DirectoryNode();
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), writer, logger, CancellationToken.None);

			await writer.FlushAsync(node, CancellationToken.None);
			return 0;
		}
	}
}
