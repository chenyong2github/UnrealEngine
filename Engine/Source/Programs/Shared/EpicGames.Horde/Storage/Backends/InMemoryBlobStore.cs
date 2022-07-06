// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Backends
{
	/// <summary>
	/// Implementation of <see cref="IBlobStore"/> which stores data in memory. Not intended for production use.
	/// </summary>
	public class InMemoryBlobStore : IBlobStore
	{
		/// <summary>
		/// Map of blob id to blob data
		/// </summary>
		readonly ConcurrentDictionary<BlobId, IBlob> _blobs = new ConcurrentDictionary<BlobId, IBlob>();

		/// <summary>
		/// Map of ref name to ref data
		/// </summary>
		readonly ConcurrentDictionary<RefId, IBlob> _refs = new ConcurrentDictionary<RefId, IBlob>();

		/// <inheritdoc cref="_blobs"/>
		public IReadOnlyDictionary<BlobId, IBlob> Blobs => _blobs;

		/// <inheritdoc cref="_refs"/>
		public IReadOnlyDictionary<RefId, IBlob> Refs => _refs;

		#region Blobs

		/// <inheritdoc/>
		public Task<IBlob?> TryReadBlobAsync(BlobId blobId, CancellationToken cancellationToken = default)
		{
			_blobs.TryGetValue(blobId, out IBlob? blob);
			return Task.FromResult(blob);
		}

		/// <inheritdoc/>
		public Task<BlobId> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			BlobId blobId = new BlobId(Guid.NewGuid().ToString());
			_blobs[blobId] = BlobUtils.FromMemory(data.ToArray(), references);
			return Task.FromResult(blobId);
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public Task DeleteRefAsync(RefId id, CancellationToken cancellationToken) => Task.FromResult(_refs.TryRemove(id, out _));

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(RefId id, CancellationToken cancellationToken) => Task.FromResult(_refs.ContainsKey(id));

		/// <inheritdoc/>
		public Task<IBlob?> TryReadRefAsync(RefId id, CancellationToken cancellationToken)
		{
			_refs.TryGetValue(id, out IBlob? blob);
			return Task.FromResult(blob);
		}

		/// <inheritdoc/>
		public Task WriteRefAsync(RefId id, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken)
		{
			_refs[id] = BlobUtils.FromMemory(data.ToArray(), references);
			return Task.CompletedTask;
		}

		#endregion
	}
}
