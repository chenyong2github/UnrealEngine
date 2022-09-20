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
		readonly ConcurrentDictionary<RefName, BlobLocator> _refs = new ConcurrentDictionary<RefName, BlobLocator>();

		/// <inheritdoc cref="_blobs"/>
		public IReadOnlyDictionary<BlobLocator, Bundle> Blobs => _blobs;

		/// <inheritdoc cref="_refs"/>
		public IReadOnlyDictionary<RefName, BlobLocator> Refs => _refs;

		/// <summary>
		/// Constructor
		/// </summary>
		public InMemoryBlobStore() 
			: base(null)
		{
		}

		#region Blobs

		/// <inheritdoc/>
		public override Task<Bundle> ReadBundleAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			return Task.FromResult(_blobs[locator]);
		}

		/// <inheritdoc/>
		public override Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = BlobLocator.Create(HostId.Empty, prefix);
			_blobs[locator] = bundle;
			return Task.FromResult(locator);
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public override Task DeleteRefAsync(RefName name, CancellationToken cancellationToken) => Task.FromResult(_refs.TryRemove(name, out _));

		/// <inheritdoc/>
		public override Task<BlobLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			if (_refs.TryGetValue(name, out BlobLocator locator))
			{
				return Task.FromResult(locator);
			}
			else
			{
				return Task.FromResult(BlobLocator.Empty);
			}
		}

		/// <inheritdoc/>
		public override Task WriteRefTargetAsync(RefName name, BlobLocator locator, CancellationToken cancellationToken = default)
		{
			_refs[name] = locator;
			return Task.CompletedTask;
		}

		#endregion
	}
}
