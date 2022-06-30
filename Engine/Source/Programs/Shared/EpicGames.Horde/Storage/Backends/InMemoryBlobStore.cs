// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Impl
{
	/// <summary>
	/// Implementation of <see cref="IBlobStore"/> which stores data in memory. Not intended for production use.
	/// </summary>
	public class InMemoryBlobStore : IBlobStore
	{
		class Blob : IBlob
		{
			readonly ReadOnlyMemory<byte> _data;
			readonly IReadOnlyList<BlobId> _references;

			public Blob(ReadOnlyMemory<byte> data, IReadOnlyList<BlobId> references)
			{
				_data = data;
				_references = references;
			}

			/// <inheritdoc/>
			public ValueTask<ReadOnlyMemory<byte>> GetDataAsync() => new ValueTask<ReadOnlyMemory<byte>>(_data);

			/// <inheritdoc/>
			public ValueTask<IReadOnlyList<BlobId>> GetReferencesAsync() => new ValueTask<IReadOnlyList<BlobId>>(_references);
		}

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
		public Task<IBlob> ReadBlobAsync(BlobId blobId, CancellationToken cancellationToken = default) => Task.FromResult(_blobs[blobId]);

		/// <inheritdoc/>
		public Task<BlobId> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			BlobId blobId = new BlobId(Guid.NewGuid().ToString());
			Blob blob = new Blob(data.ToArray(), references);
			_blobs[blobId] = blob;
			return Task.FromResult(blobId);
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public Task<bool> DeleteRefAsync(RefId id, CancellationToken cancellationToken) => Task.FromResult(_refs.TryRemove(id, out _));

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(RefId id, CancellationToken cancellationToken) => Task.FromResult(_refs.ContainsKey(id));

		/// <inheritdoc/>
		public Task<IBlob> ReadRefAsync(RefId id, CancellationToken cancellationToken) => Task.FromResult(_refs[id]);

		/// <inheritdoc/>
		public Task WriteRefAsync(RefId id, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken)
		{
			_refs[id] = new Blob(data.ToArray(), references);
			return Task.CompletedTask;
		}

		#endregion
	}
}
