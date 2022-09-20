// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Caching.Memory;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for an implementation of <see cref="IStorageClient"/>, providing implementations for some common functionality.
	/// </summary>
	public abstract class StorageClientBase : IStorageClient
	{
		readonly TreeStore _treeStore;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="cache"></param>
		protected StorageClientBase(IMemoryCache? cache)
		{
			_treeStore = new TreeStore(this, cache);
		}

		#region Blobs

		/// <inheritdoc/>
		public abstract Task<Bundle> ReadBundleAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Trees

		/// <inheritdoc/>
		public ITreeWriter CreateTreeWriter(TreeOptions options, Utf8String prefix = default)
		{
			return _treeStore.CreateTreeWriter(options, prefix);
		}

		/// <inheritdoc/>
		public virtual Task<ITreeBlob?> TryReadTreeAsync(RefName name, TimeSpan maxAge = default, CancellationToken cancellationToken = default)
		{
			return _treeStore.TryReadTreeAsync(name, maxAge, cancellationToken);
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public abstract Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<BundleRef?> TryReadRefAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (locator.IsValid())
			{
				return new BundleRef(locator, await ReadBundleAsync(locator, cancellationToken));
			}
			else
			{
				return null;
			}
		}

		/// <inheritdoc/>
		public abstract Task<BlobLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<BlobLocator> WriteRefAsync(RefName name, Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await WriteBundleAsync(bundle, prefix, cancellationToken);
			await WriteRefTargetAsync(name, locator, cancellationToken);
			return locator;
		}

		/// <inheritdoc/>
		public abstract Task WriteRefTargetAsync(RefName name, BlobLocator locator, CancellationToken cancellationToken = default);

		#endregion
	}
}
