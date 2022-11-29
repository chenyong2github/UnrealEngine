// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for an implementation of <see cref="IStorageClient"/>, providing implementations for some common functionality.
	/// </summary>
	public abstract class StorageClientBase : IStorageClient
	{
		/// <summary>
		/// Constructor
		/// </summary>
		protected StorageClientBase()
		{
		}

		#region Blobs

		/// <inheritdoc/>
		public abstract Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Refs

		/// <inheritdoc/>
		public abstract Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<RefTarget?> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<RefTarget> WriteRefAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await this.WriteBundleAsync(bundle, prefix, cancellationToken);
			RefTarget target = new RefTarget(bundle.Header.Exports[exportIdx].Hash, new NodeLocator(locator, exportIdx));
			await WriteRefTargetAsync(name, target, options, cancellationToken);
			return target;
		}

		/// <inheritdoc/>
		public abstract Task WriteRefTargetAsync(RefName name, RefTarget target, RefOptions? options = null, CancellationToken cancellationToken = default);

		#endregion
	}
}
