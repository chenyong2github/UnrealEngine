// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
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
		public abstract Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Bundles

		/// <inheritdoc/>
		public virtual async Task<Bundle> ReadBundleAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			using Stream stream = await ReadBlobAsync(locator, cancellationToken);
			return await Bundle.FromStreamAsync(stream, cancellationToken);
		}

		/// <inheritdoc/>
		public virtual async Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			using ReadOnlySequenceStream stream = new ReadOnlySequenceStream(bundle.AsSequence());
			return await WriteBlobAsync(stream, prefix, cancellationToken);
		}

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
		public virtual async Task<RefValue?> TryReadRefValueAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			RefTarget? refTarget = await TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (refTarget == null)
			{
				return null;
			}
			return new RefValue(refTarget, await ReadBundleAsync(refTarget.Locator, cancellationToken));
		}

		/// <inheritdoc/>
		public abstract Task<RefTarget?> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<RefTarget> WriteRefValueAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await WriteBundleAsync(bundle, prefix, cancellationToken);
			RefTarget target = new RefTarget(locator, exportIdx);
			await WriteRefTargetAsync(name, target, cancellationToken);
			return target;
		}

		/// <inheritdoc/>
		public abstract Task WriteRefTargetAsync(RefName name, RefTarget target, CancellationToken cancellationToken = default);

		#endregion
	}
}
