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
	/// Base class for an implementation of <see cref="IStorageClient"/>, providing implementations for some common functionality.
	/// </summary>
	public abstract class StorageClientBase : IStorageClient
	{
		/// <summary>
		/// Reader for node data
		/// </summary>
		protected TreeReader TreeReader { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		protected StorageClientBase(IMemoryCache? memoryCache, ILogger logger)
		{
			TreeReader = new TreeReader(this, memoryCache, logger);
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
		public IStorageWriter CreateWriter(RefName refName = default, TreeOptions? options = null)
		{
			return new TreeWriter(this, TreeReader, refName, options);
		}

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
		public abstract Task<NodeHandle?> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<NodeHandle> WriteRefAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await this.WriteBundleAsync(bundle, prefix, cancellationToken);
			NodeHandle target = new NodeHandle(TreeReader, new NodeLocator(locator, exportIdx));
			await WriteRefTargetAsync(name, target, options, cancellationToken);
			return target;
		}

		/// <inheritdoc/>
		public abstract Task WriteRefTargetAsync(RefName name, NodeHandle target, RefOptions? options = null, CancellationToken cancellationToken = default);

		#endregion
	}
}
