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
		readonly ConcurrentDictionary<RefName, IBlob> _refs = new ConcurrentDictionary<RefName, IBlob>();

		/// <inheritdoc cref="_blobs"/>
		public IReadOnlyDictionary<BlobId, IBlob> Blobs => _blobs;

		/// <inheritdoc cref="_refs"/>
		public IReadOnlyDictionary<RefName, IBlob> Refs => _refs;

		#region Blobs

		/// <inheritdoc/>
		public Task<IBlob?> TryReadBlobAsync(BlobId blobId, CancellationToken cancellationToken = default)
		{
			_blobs.TryGetValue(blobId, out IBlob? blob);
			return Task.FromResult(blob);
		}

		/// <inheritdoc/>
		public Task<BlobId> WriteBlobAsync(RefName refName, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			BlobId blobId = new BlobId(Guid.NewGuid().ToString());
			_blobs[blobId] = BlobUtils.FromMemory(data.ToArray(), references);
			return Task.FromResult(blobId);
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public Task DeleteRefAsync(RefName name, CancellationToken cancellationToken) => Task.FromResult(_refs.TryRemove(name, out _));

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(RefName name, CancellationToken cancellationToken) => Task.FromResult(_refs.ContainsKey(name));

		/// <inheritdoc/>
		public Task<IBlob?> TryReadRefAsync(RefName name, CancellationToken cancellationToken)
		{
			_refs.TryGetValue(name, out IBlob? blob);
			return Task.FromResult(blob);
		}

		/// <inheritdoc/>s
		public Task WriteRefAsync(RefName name, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken)
		{
			_refs[name] = BlobUtils.FromMemory(data.ToArray(), references);
			return Task.CompletedTask;
		}

		#endregion
	}
}
