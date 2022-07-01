// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
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
		class FileBlob : IBlob
		{
			readonly IReadOnlyList<BlobId> _references;
			readonly ReadOnlyMemory<byte> _data;

			public FileBlob(IReadOnlyList<BlobId> references, ReadOnlyMemory<byte> data)
			{
				_references = references;
				_data = data;
			}

			public static async Task<FileBlob> ReadAsync(FileReference file, CancellationToken cancellationToken)
			{
				byte[] bytes = await FileReference.ReadAllBytesAsync(file, cancellationToken);
				MemoryReader reader = new MemoryReader(bytes);
				IReadOnlyList<BlobId> references = reader.ReadVariableLengthArray(() => new BlobId(reader.ReadUtf8String()));
				ReadOnlyMemory<byte> data = reader.ReadVariableLengthBytes();
				return new FileBlob(references, data);
			}

			public async Task WriteAsync(FileReference file, CancellationToken cancellationToken)
			{
				ByteArrayBuilder writer = new ByteArrayBuilder();
				writer.WriteVariableLengthArray(_references, x => writer.WriteUtf8String(x.Inner));
				writer.WriteVariableLengthBytes(_data.Span);
				await FileReference.WriteAllBytesAsync(file, writer.ToByteArray(), cancellationToken);
			}

			/// <inheritdoc/>
			public ValueTask<ReadOnlyMemory<byte>> GetDataAsync() => new ValueTask<ReadOnlyMemory<byte>>(_data);

			/// <inheritdoc/>
			public ValueTask<IReadOnlyList<BlobId>> GetReferencesAsync() => new ValueTask<IReadOnlyList<BlobId>>(_references);
		}

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

			FileReference GetRefFile(RefId id) => FileReference.Combine(_rootDir, id.Hash.ToString() + ".ref");
			FileReference GetBlobFile(BlobId id) => FileReference.Combine(_rootDir, id.Inner.ToString() + ".blob");

			public async Task<IBlob> ReadBlobAsync(BlobId id, CancellationToken cancellationToken = default)
			{
				FileReference file = GetBlobFile(id);
				_logger.LogInformation("Reading {File}", file);
				return await FileBlob.ReadAsync(file, cancellationToken);
			}

			public Task<bool> DeleteRefAsync(RefId id, CancellationToken cancellationToken = default)
			{
				throw new NotImplementedException();
			}

			public Task<bool> HasRefAsync(RefId id, CancellationToken cancellationToken = default)
			{
				throw new NotImplementedException();
			}

			public async Task<IBlob> ReadRefAsync(RefId id, CancellationToken cancellationToken = default)
			{
				FileReference file = GetRefFile(id);
				_logger.LogInformation("Reading {File}", file);
				return await FileBlob.ReadAsync(file, cancellationToken);
			}

			public async Task<BlobId> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
			{
				BlobId id = new BlobId(Guid.NewGuid().ToString());
				FileReference file = GetBlobFile(id);
				_logger.LogInformation("Writing {File}", file);
				await new FileBlob(references, data.AsSingleSegment()).WriteAsync(file, cancellationToken);
				return id;
			}

			public async Task WriteRefAsync(RefId id, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
			{
				FileReference file = GetRefFile(id);
				_logger.LogInformation("Writing {File}", file);
				await new FileBlob(references, data.AsSingleSegment()).WriteAsync(file, cancellationToken);
			}
		}

		public static RefId DefaultRefId { get; } = new RefId("default-ref");

		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference StorageDir { get; set; } = DirectoryReference.Combine(Program.AppDir, "bundles");

		IBlobStore? _blobStore;

		protected IBlobStore CreateBlobStore(ILogger logger)
		{
			_blobStore ??= new FileBlobStore(StorageDir, logger);
			return _blobStore;
		}

		protected BundleStore<T> CreateTreeStore<T>(ILogger logger, IMemoryCache cache) where T : TreeNode
		{
			IBlobStore blobStore = CreateBlobStore(logger);
			return new BundleStore<T>(blobStore, new BundleOptions(), cache);
		}
	}

	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class CreateCommand : BundleCommandBase
	{
		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = DefaultRefId;

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			using ITreeStore<DirectoryNode> store = CreateTreeStore<DirectoryNode>(logger, cache);

			DirectoryNode node = new DirectoryNode();
			await node.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), logger, CancellationToken.None);
			await store.WriteTreeAsync(RefId, node);

			return 0;
		}
	}
}
