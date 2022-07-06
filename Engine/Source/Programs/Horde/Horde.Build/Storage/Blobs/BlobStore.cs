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
	/// Implementation of <see cref="IBlobStore"/> for AWS
	/// </summary>
	public class BlobStore : IBlobStore
	{
		readonly IStorageBackend _backend;
		readonly string _blobPrefix;
		readonly string _refPrefix;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="backend">Backend to use for storing data</param>
		/// <param name="blobPrefix">Prefix to append to any blob ids</param>
		/// <param name="refPrefix">Prefix to append to any ref ids</param>
		public BlobStore(IStorageBackend backend, string blobPrefix, string refPrefix)
		{
			_backend = backend;
			_blobPrefix = blobPrefix;
			_refPrefix = refPrefix;
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

		string GetBlobPath(BlobId id) => $"{_blobPrefix}{id}";

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

		string GetRefPath(RefId id) => $"{_refPrefix}{id}";

		/// <inheritdoc/>
		public async Task DeleteRefAsync(RefId id, CancellationToken cancellationToken = default)
		{
			await _backend.DeleteAsync(GetRefPath(id), cancellationToken);
		}

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(RefId id, CancellationToken cancellationToken = default)
		{
			throw new System.NotImplementedException();
		}

		/// <inheritdoc/>
		public Task<IBlob?> TryReadRefAsync(RefId id, CancellationToken cancellationToken = default)
		{
			throw new System.NotImplementedException();
		}

		/// <inheritdoc/>
		public Task WriteRefAsync(RefId id, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			throw new System.NotImplementedException();
		}

		#endregion
	}
}
