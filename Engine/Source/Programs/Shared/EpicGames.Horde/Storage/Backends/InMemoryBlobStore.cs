// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Caching.Memory;

namespace EpicGames.Horde.Storage.Backends
{
	/// <summary>
	/// Implementation of <see cref="IStorageClient"/> which stores data in memory. Not intended for production use.
	/// </summary>
	public class InMemoryBlobStore : StorageClientBase
	{
		/// <summary>
		/// Map of blob id to blob data
		/// </summary>
		readonly ConcurrentDictionary<BlobLocator, Bundle> _blobs = new ConcurrentDictionary<BlobLocator, Bundle>();

		/// <summary>
		/// Map of ref name to ref data
		/// </summary>
		readonly ConcurrentDictionary<RefName, NodeLocator> _refs = new ConcurrentDictionary<RefName, NodeLocator>();

		/// <inheritdoc cref="_blobs"/>
		public IReadOnlyDictionary<BlobLocator, Bundle> Blobs => _blobs;

		/// <inheritdoc cref="_refs"/>
		public IReadOnlyDictionary<RefName, NodeLocator> Refs => _refs;

		/// <summary>
		/// Constructor
		/// </summary>
		public InMemoryBlobStore(IMemoryCache cache) 
			: base(cache)
		{
		}

		#region Blobs

		/// <inheritdoc/>
		public override Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			Stream stream = new ReadOnlySequenceStream(_blobs[locator].AsSequence());
			return Task.FromResult(stream);
		}

		/// <inheritdoc/>
		public override async Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = BlobLocator.Create(HostId.Empty, prefix);
			_blobs[locator] = await Bundle.FromStreamAsync(stream, cancellationToken);
			return locator;
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public override Task DeleteRefAsync(RefName name, CancellationToken cancellationToken) => Task.FromResult(_refs.TryRemove(name, out _));

		/// <inheritdoc/>
		public override Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			NodeLocator refTarget;
			_refs.TryGetValue(name, out refTarget);
			return Task.FromResult(refTarget);
		}

		/// <inheritdoc/>
		public override Task WriteRefTargetAsync(RefName name, NodeLocator target, CancellationToken cancellationToken = default)
		{
			_refs[name] = target;
			return Task.CompletedTask;
		}

		#endregion
	}
}
