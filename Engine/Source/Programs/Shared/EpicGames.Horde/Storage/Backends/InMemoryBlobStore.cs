// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

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
		readonly ConcurrentDictionary<BlobLocator, IBlob> _blobs = new ConcurrentDictionary<BlobLocator, IBlob>();

		/// <summary>
		/// Map of ref name to ref data
		/// </summary>
		readonly ConcurrentDictionary<RefName, BlobLocator> _refs = new ConcurrentDictionary<RefName, BlobLocator>();

		/// <inheritdoc cref="_blobs"/>
		public IReadOnlyDictionary<BlobLocator, IBlob> Blobs => _blobs;

		/// <inheritdoc cref="_refs"/>
		public IReadOnlyDictionary<RefName, BlobLocator> Refs => _refs;

		#region Blobs

		/// <inheritdoc/>
		public Task<IBlob> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			return Task.FromResult(_blobs[locator]);
		}

		/// <inheritdoc/>
		public Task<BlobLocator> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<BlobLocator> references, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator blobId = BlobLocator.Create(HostId.Empty, prefix);

			IBlob blob = Blob.FromMemory(blobId, data.ToArray(), references);
			_blobs[blobId] = blob;

			return Task.FromResult(blobId);
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public Task DeleteRefAsync(RefName name, CancellationToken cancellationToken) => Task.FromResult(_refs.TryRemove(name, out _));

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(RefName name, CancellationToken cancellationToken) => Task.FromResult(_refs.ContainsKey(name));

		/// <inheritdoc/>
		public Task<IBlob?> TryReadRefAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			IBlob? blob = null;
			if (_refs.TryGetValue(name, out BlobLocator locator))
			{
				_blobs.TryGetValue(locator, out blob);
			}
			return Task.FromResult(blob);
		}

		/// <inheritdoc/>
		public Task<BlobLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			if (_refs.TryGetValue(name, out BlobLocator blobId))
			{
				return Task.FromResult(blobId);
			}
			else
			{
				return Task.FromResult(BlobLocator.Empty);
			}
		}

		/// <inheritdoc/>
		public async Task<BlobLocator> WriteRefAsync(RefName name, ReadOnlySequence<byte> data, IReadOnlyList<BlobLocator> references, CancellationToken cancellationToken)
		{
			BlobLocator locator = await WriteBlobAsync(data, references, name.Text, cancellationToken);
			_refs[name] = locator;
			return locator;
		}

		/// <inheritdoc/>
		public Task WriteRefTargetAsync(RefName name, BlobLocator blobId, CancellationToken cancellationToken = default)
		{
			_refs[name] = blobId;
			return Task.CompletedTask;
		}

		#endregion
	}
}
