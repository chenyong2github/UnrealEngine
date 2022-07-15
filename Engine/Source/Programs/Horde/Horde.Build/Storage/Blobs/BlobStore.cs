// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;

namespace Horde.Build.Storage
{
	/// <summary>
	/// Options for configuring the default blob store implementation
	/// </summary>
	public interface IBlobStoreOptions
	{
		/// <summary>
		/// Prefix for storing blobs
		/// </summary>
		string BlobPrefix { get; }

		/// <summary>
		/// Prefix for storing refs
		/// </summary>
		string RefPrefix { get; }
	}

	/// <summary>
	/// Implementation of <see cref="IBlobStore"/> for AWS
	/// </summary>
	public class BlobStore : IBlobStore
	{
		readonly IStorageBackend _backend;
		readonly IBlobStoreOptions _options;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="backend">Backend to use for storing data</param>
		/// <param name="options">Options for the blob store</param>
		public BlobStore(IStorageBackend backend, IBlobStoreOptions options)
		{
			_backend = backend;
			_options = options;
		}

		async Task<IBlob?> TryReadAsync(string path, CancellationToken cancellationToken = default)
		{
			ReadOnlyMemory<byte>? data = await _backend.ReadBytesAsync(path, cancellationToken);
			if (data == null)
			{
				return null;
			}
			return BlobUtils.Deserialize(data.Value);
		}

		async Task WriteAsync(string path, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			ReadOnlySequence<byte> sequence = BlobUtils.Serialize(data, references);
			await _backend.WriteBytesAsync(path, sequence.AsSingleSegment(), cancellationToken);
		}

		#region Blobs

		string GetBlobPath(BlobId id) => $"{_options.BlobPrefix}{id}";

		/// <inheritdoc/>
		public Task<IBlob?> TryReadBlobAsync(BlobId id, CancellationToken cancellationToken = default) => TryReadAsync(GetBlobPath(id), cancellationToken);

		/// <inheritdoc/>
		public async Task<BlobId> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			BlobId id = new BlobId(Guid.NewGuid().ToString());
			await WriteAsync(GetBlobPath(id), data, references, cancellationToken);
			return id;
		}

		#endregion

		#region Refs

		string GetRefPath(RefName name) => $"{_options.RefPrefix}{name}";

		/// <inheritdoc/>
		public async Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			await _backend.DeleteAsync(GetRefPath(name), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<bool> HasRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			return await _backend.ExistsAsync(GetRefPath(name), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<IBlob?> TryReadRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			return await TryReadAsync(GetRefPath(name), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task WriteRefAsync(RefName name, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			await WriteAsync(GetRefPath(name), data, references, cancellationToken);
		}

		#endregion
	}
}
