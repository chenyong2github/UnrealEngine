// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for an implementation of <see cref="IStorageClient"/>, providing implementations for some common functionality using bundles.
	/// </summary>
	public abstract class BundleStorageClient : IStorageClient
	{
		/// <summary>
		/// Reader for node data
		/// </summary>
		protected BundleReader TreeReader { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		protected BundleStorageClient(IMemoryCache? memoryCache, ILogger logger)
		{
			TreeReader = new BundleReader(this, memoryCache, logger);
		}

		#region Blobs

		/// <inheritdoc/>
		public abstract Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Nodes

		/// <inheritdoc/>
		public BundleWriter CreateWriter(RefName refName = default, BundleOptions? options = null)
		{
			return new BundleWriter(this, TreeReader, refName, options);
		}

		/// <inheritdoc/>
		IStorageWriter IStorageClient.CreateWriter(RefName refName) => CreateWriter(refName);

		#endregion

		#region Aliases

		/// <inheritdoc/>
		public abstract Task AddAliasAsync(Utf8String name, NodeHandle locator, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task RemoveAliasAsync(Utf8String name, NodeHandle locator, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract IAsyncEnumerable<NodeHandle> FindNodesAsync(Utf8String name, CancellationToken cancellationToken = default);

		#endregion

		#region Refs

		/// <inheritdoc/>
		public abstract Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task<NodeHandle?> TryReadRefTargetAsync(RefName name, RefCacheTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public abstract Task WriteRefTargetAsync(RefName name, NodeHandle target, RefOptions? options = null, CancellationToken cancellationToken = default);

		#endregion
	}
}
