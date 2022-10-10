// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Reflection.Metadata;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Exception for a ref not existing
	/// </summary>
	public sealed class RefNameNotFoundException : Exception
	{
		/// <summary>
		/// Name of the missing ref
		/// </summary>
		public RefName Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="name"></param>
		public RefNameNotFoundException(RefName name)
			: base($"Ref name '{name}' not found")
		{
			Name = name;
		}
	}

	/// <summary>
	/// Value for a ref
	/// </summary>
	[DebuggerDisplay("{Target}")]
	public class RefValue
	{
		/// <summary>
		/// Target for the ref
		/// </summary>
		public NodeLocator Target { get; }

		/// <summary>
		/// Bundle referenced as the target
		/// </summary>
		public Bundle Bundle { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RefValue(NodeLocator target, Bundle bundle)
		{
			Target = target;
			Bundle = bundle;
		}
	}

	/// <summary>
	/// Locates a node in storage
	/// </summary>
	public struct NodeLocator
	{
		/// <summary>
		/// Location of the blob containing this node
		/// </summary>
		public BlobLocator Blob { get; }

		/// <summary>
		/// Index of the export within the blob
		/// </summary>
		public int ExportIdx { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public NodeLocator(BlobLocator blob, int exportIdx)
		{
			Blob = blob;
			ExportIdx = exportIdx;
		}

		/// <summary>
		/// Determines if this locator points to a valid entry
		/// </summary>
		public bool IsValid() => Blob.IsValid();

		/// <inheritdoc/>
		public override string ToString() => $"{Blob}#{ExportIdx}";
	}

	/// <summary>
	/// Base interface for a low-level storage backend. Blobs added to this store are not content addressed, but referenced by <see cref="BlobLocator"/>.
	/// </summary>
	public interface IStorageClient
	{
		#region Blobs

		/// <summary>
		/// Reads raw data for a blob from the store
		/// </summary>
		/// <param name="locator">The blob locator</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Stream containing the data</returns>
		Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads a ranged chunk from a blob
		/// </summary>
		/// <param name="locator">Locator for the blob</param>
		/// <param name="offset">Starting offset for the data to read</param>
		/// <param name="length">Length of the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new blob to the store
		/// </summary>
		/// <param name="stream">Blob data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <param name="prefix">Prefix for blob names. While the returned BlobId is guaranteed to be unique, this name can be used as a prefix to aid debugging.</param>
		/// <returns>Unique identifier for the blob</returns>
		Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Bundles

		/// <summary>
		/// Reads data for a bundle from the store
		/// </summary>
		/// <param name="locator">The blob identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<Bundle> ReadBundleAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new bundle to the store
		/// </summary>
		/// <param name="bundle">Bundle data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <param name="prefix">Prefix for blob names. While the returned BlobId is guaranteed to be unique, this name can be used as a prefix to aid debugging.</param>
		/// <returns>Unique identifier for the blob</returns>
		Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Nodes

		/// <summary>
		/// Reads a node from a bundle
		/// </summary>
		/// <param name="locator">Locator for the node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Node data read from the given bundle</returns>
		ValueTask<TNode> ReadNodeAsync<TNode>(NodeLocator locator, CancellationToken cancellationToken = default) where TNode : TreeNode;

		/// <summary>
		/// Reads data for a ref from the store, along with the node's contents.
		/// </summary>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Node for the given ref, or null if it does not exist</returns>
		Task<TNode?> TryReadNodeAsync<TNode>(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) where TNode : TreeNode;

		#endregion

		#region Refs

		/// <summary>
		/// Reads data for a ref from the store
		/// </summary>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Node pointed to by the ref</returns>
		Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new ref to the store which points to a new blob
		/// </summary>
		/// <param name="name">Ref to write</param>
		/// <param name="bundle">The bundle to write</param>
		/// <param name="exportIdx">Index of the export in the bundle to be the root of the tree</param>
		/// <param name="prefix">Prefix for blob names.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the blob</returns>
		Task<NodeLocator> WriteRefAsync(RefName name, Bundle bundle, int exportIdx, Utf8String prefix = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new ref to the store
		/// </summary>
		/// <param name="name">Ref to write</param>
		/// <param name="target">The target for the ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the blob</returns>
		Task WriteRefTargetAsync(RefName name, NodeLocator target, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads data for a ref from the store
		/// </summary>
		/// <param name="name">The ref identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default);

		#endregion
	}

	/// <summary>
	/// Typed implementation of <see cref="IStorageClient"/> for use with dependency injection
	/// </summary>
	public interface IStorageClient<T> : IStorageClient
	{
	}

	/// <summary>
	/// Extension methods for <see cref="IStorageClient"/>
	/// </summary>
	public static class StorageClientExtensions
	{
		class TypedStorageClient<T> : IStorageClient<T>
		{
			readonly IStorageClient _inner;

			public TypedStorageClient(IStorageClient inner) => _inner = inner;

			#region Blobs

			/// <inheritdoc/>
			public Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default) => _inner.ReadBlobAsync(locator, cancellationToken);

			/// <inheritdoc/>
			public Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default) => _inner.ReadBlobRangeAsync(locator, offset, length, cancellationToken);

			/// <inheritdoc/>
			public Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default) => _inner.WriteBlobAsync(stream, prefix, cancellationToken);

			#endregion

			#region Bundles

			/// <inheritdoc/>
			public Task<Bundle> ReadBundleAsync(BlobLocator locator, CancellationToken cancellationToken = default) => _inner.ReadBundleAsync(locator, cancellationToken);

			/// <inheritdoc/>
			public Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default) => _inner.WriteBundleAsync(bundle, prefix, cancellationToken);

			#endregion

			#region Trees

			/// <inheritdoc/>
			public ValueTask<TNode> ReadNodeAsync<TNode>(NodeLocator locator, CancellationToken cancellationToken = default) where TNode : TreeNode => _inner.ReadNodeAsync<TNode>(locator, cancellationToken);

			#endregion

			#region Refs

			/// <inheritdoc/>
			public Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default) => _inner.DeleteRefAsync(name, cancellationToken);

			/// <inheritdoc/>
			public Task<TNode?> TryReadNodeAsync<TNode>(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) where TNode : TreeNode => _inner.TryReadNodeAsync<TNode>(name, cacheTime, cancellationToken);

			/// <inheritdoc/>
			public Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) => _inner.TryReadRefTargetAsync(name, cacheTime, cancellationToken);

			/// <inheritdoc/>
			public Task<NodeLocator> WriteRefAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, CancellationToken cancellationToken = default) => _inner.WriteRefAsync(name, bundle, exportIdx, prefix, cancellationToken);

			/// <inheritdoc/>
			public Task WriteRefTargetAsync(RefName name, NodeLocator target, CancellationToken cancellationToken = default) => _inner.WriteRefTargetAsync(name, target, cancellationToken);

			#endregion
		}

		/// <summary>
		/// Wraps a <see cref="IStorageClient"/> interface with a type argument
		/// </summary>
		/// <param name="blobStore">Regular blob store instance</param>
		/// <returns></returns>
		public static IStorageClient<T> ForType<T>(this IStorageClient blobStore) => new TypedStorageClient<T>(blobStore);

		#region Nodes

		/// <summary>
		/// Attempts to reads a ref from the store
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Name of the ref</param>
		/// <param name="maxAge">Maximum age of any cached ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref value</returns>
		public static Task<TNode?> TryReadNodeAsync<TNode>(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default) where TNode : TreeNode
		{
			return store.TryReadNodeAsync<TNode>(name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="cacheTime">Minimum coherency of any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static async Task<TNode> ReadNodeAsync<TNode>(this IStorageClient store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) where TNode : TreeNode
		{
			TNode? refValue = await store.TryReadNodeAsync<TNode>(name, cacheTime, cancellationToken);
			if (refValue == null)
			{
				throw new RefNameNotFoundException(name);
			}
			return refValue;
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age for any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref value</returns>
		public static Task<TNode> ReadNodeAsync<TNode>(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default) where TNode : TreeNode
		{
			return ReadNodeAsync<TNode>(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Writes a node to storage
		/// </summary>
		/// <param name="store">Store instance to write to</param>
		/// <param name="name">Name of the ref containing this node</param>
		/// <param name="node">Node to be written</param>
		/// <param name="options">Options for the node writer</param>
		/// <param name="prefix">Prefix for uploaded blobs</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Location of node targetted by the ref</returns>
		public static async Task<NodeLocator> WriteNodeAsync(this IStorageClient store, RefName name, TreeNode node, TreeOptions? options = null, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			TreeWriter writer = new TreeWriter(store, options, prefix.IsEmpty ? name.Text : prefix);
			return await writer.WriteRefAsync(name, node, cancellationToken);
		}

		#endregion

		#region Refs

		/// <summary>
		/// Checks if the given ref exists
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Name of the reference to look for</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the ref exists, false if it did not exist</returns>
		public static async Task<bool> HasRefAsync(this IStorageClient store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			NodeLocator locator = await store.TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			return locator.IsValid();
		}

		/// <summary>
		/// Checks if the given ref exists
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Name of the reference to look for</param>
		/// <param name="maxAge">Maximum age of any cached ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the ref exists, false if it did not exist</returns>
		public static Task<bool> HasRefAsync(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return HasRefAsync(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Attempts to reads a ref from the store
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age of any cached ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref target</returns>
		public static Task<NodeLocator> TryReadRefTargetAsync(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return store.TryReadRefTargetAsync(name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="cacheTime">Minimum coherency of any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref target</returns>
		public static async Task<NodeLocator> ReadRefTargetAsync(this IStorageClient store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			NodeLocator refTarget = await store.TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (!refTarget.IsValid())
			{
				throw new RefNameNotFoundException(name);
			}
			return refTarget;
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age for any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static Task<NodeLocator> ReadRefTargetAsync(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return ReadRefTargetAsync(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		#endregion
	}
}
