// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.CodeAnalysis.Rename;

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
	/// Target for a ref
	/// </summary>
	[DebuggerDisplay("{Locator}#{Export}")]
	public class RefTarget
	{
		/// <summary>
		/// Location of the blob
		/// </summary>
		public BlobLocator Locator { get; }

		/// <summary>
		/// Index of the blob export
		/// </summary>
		public int ExportIdx { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RefTarget(BlobLocator locator, int exportIdx)
		{
			Locator = locator;
			ExportIdx = exportIdx;
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
		public RefTarget Target { get; }

		/// <summary>
		/// Bundle referenced as the target
		/// </summary>
		public Bundle Bundle { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RefValue(RefTarget target, Bundle bundle)
		{
			Target = target;
			Bundle = bundle;
		}
	}

	/// <summary>
	/// Base interface for a low-level storage backend. Blobs added to this store are not content addressed, but referenced by <see cref="BlobLocator"/>.
	/// </summary>
	public interface IStorageClient
	{
		#region Blobs

		/// <summary>
		/// Reads data for a blob from the store
		/// </summary>
		/// <param name="locator">The blob locator</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Stream containing the data</returns>
		Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads part of a blob from storage
		/// </summary>
		/// <param name="locator">The blob locator</param>
		/// <param name="offset">Offset within the blob to read from</param>
		/// <param name="length">Maximum length of data to read. The stream will be truncated if the blob is shorter than this.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Stream containing the data</returns>
		Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset = 0, int length = Int32.MaxValue, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new blob to the store
		/// </summary>
		/// <param name="stream">Blob data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <param name="prefix">Prefix for blob names. While the returned BlobId is guaranteed to be unique, this name can be used as a prefix to aid debugging.</param>
		/// <returns>Unique identifier for the blob</returns>
		Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Trees

		/// <summary>
		/// Creates a new context for reading trees.
		/// </summary>
		/// <param name="options">Options for the tree writer</param>
		/// <param name="prefix">Prefix for blob names.</param>
		/// <returns>New context instance</returns>
		ITreeWriter CreateTreeWriter(TreeOptions options, Utf8String prefix = default);

		/// <summary>
		/// Attempts to read a tree with the given name
		/// </summary>
		/// <param name="name">Name of the ref used to store the tree</param>
		/// <param name="maxAge"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Reference to the root of the tree. Null if the ref does not exist.</returns>
		Task<ITreeBlob?> TryReadTreeAsync(RefName name, TimeSpan maxAge = default, CancellationToken cancellationToken = default);

		#endregion

		#region Refs

		/// <summary>
		/// Reads data for a ref from the store, along with the blob contents.
		/// </summary>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<RefValue?> TryReadRefValueAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads data for a ref from the store
		/// </summary>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<RefTarget?> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new ref to the store which points to a new blob
		/// </summary>
		/// <param name="name">Ref to write</param>
		/// <param name="bundle">Bundle to write</param>
		/// <param name="exportIdx">Index of the export to target for the ref</param>
		/// <param name="prefix">Prefix for blob names.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the blob</returns>
		Task<RefTarget> WriteRefValueAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new ref to the store
		/// </summary>
		/// <param name="name">Ref to write</param>
		/// <param name="target">The target for the ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the blob</returns>
		Task WriteRefTargetAsync(RefName name, RefTarget target, CancellationToken cancellationToken = default);

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

			#region Trees

			/// <inheritdoc/>
			public ITreeWriter CreateTreeWriter(TreeOptions options, Utf8String prefix) => _inner.CreateTreeWriter(options, prefix);

			/// <inheritdoc/>
			public Task<ITreeBlob?> TryReadTreeAsync(RefName name, TimeSpan maxAge = default, CancellationToken cancellationToken = default) => _inner.TryReadTreeAsync(name, maxAge, cancellationToken);

			#endregion

			#region Refs

			/// <inheritdoc/>
			public Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default) => _inner.DeleteRefAsync(name, cancellationToken);

			/// <inheritdoc/>
			public Task<RefValue?> TryReadRefValueAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) => _inner.TryReadRefValueAsync(name, cacheTime, cancellationToken);

			/// <inheritdoc/>
			public Task<RefTarget?> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) => _inner.TryReadRefTargetAsync(name, cacheTime, cancellationToken);

			/// <inheritdoc/>
			public Task<RefTarget> WriteRefValueAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, CancellationToken cancellationToken = default) => _inner.WriteRefValueAsync(name, bundle, exportIdx, prefix, cancellationToken);

			/// <inheritdoc/>
			public Task WriteRefTargetAsync(RefName name, RefTarget target, CancellationToken cancellationToken = default) => _inner.WriteRefTargetAsync(name, target, cancellationToken);

			#endregion
		}

		/// <summary>
		/// Wraps a <see cref="IStorageClient"/> interface with a type argument
		/// </summary>
		/// <param name="blobStore">Regular blob store instance</param>
		/// <returns></returns>
		public static IStorageClient<T> ForType<T>(this IStorageClient blobStore) => new TypedStorageClient<T>(blobStore);

		/// <summary>
		/// Reads data for a bundle from the store
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="locator">The blob identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public static async Task<Bundle> ReadBundleAsync(this IStorageClient store, BlobLocator locator, CancellationToken cancellationToken = default)
		{
			using Stream stream = await store.ReadBlobAsync(locator, cancellationToken);
			return await Bundle.FromStreamAsync(stream, cancellationToken);
		}

		/// <summary>
		/// Writes a new bundle to the store
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="bundle">Bundle data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <param name="prefix">Prefix for blob names. While the returned BlobId is guaranteed to be unique, this name can be used as a prefix to aid debugging.</param>
		/// <returns>Unique identifier for the blob</returns>
		public static async Task<BlobLocator> WriteBundleAsync(this IStorageClient store, Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			using ReadOnlySequenceStream stream = new ReadOnlySequenceStream(bundle.AsSequence());
			return await store.WriteBlobAsync(stream, prefix, cancellationToken);
		}

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
			RefTarget? locator = await store.TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			return locator != null;
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
		/// Creates a new tree writer with default options
		/// </summary>
		/// <param name="store">Store instance</param>
		/// <param name="prefix"></param>
		/// <returns></returns>
		public static ITreeWriter CreateTreeWriter(this IStorageClient store, Utf8String prefix = default)
		{
			return store.CreateTreeWriter(new TreeOptions(), prefix);
		}

		/// <summary>
		/// Attempts to reads a ref from the store
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Name of the ref</param>
		/// <param name="maxAge">Maximum age of any cached ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref value</returns>
		public static Task<RefValue?> TryReadRefValueAsync(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return store.TryReadRefValueAsync(name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Attempts to reads a ref from the store
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age of any cached ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref target</returns>
		public static Task<RefTarget?> TryReadRefTargetAsync(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
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
		/// <returns>The blob instance</returns>
		public static async Task<RefValue> ReadRefValueAsync(this IStorageClient store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			RefValue? refValue = await store.TryReadRefValueAsync(name, cacheTime, cancellationToken);
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
		public static Task<RefValue> ReadRefValueAsync(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return ReadRefValueAsync(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="cacheTime">Minimum coherency of any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref target</returns>
		public static async Task<RefTarget> ReadRefTargetAsync(this IStorageClient store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			RefTarget? refTarget = await store.TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (refTarget == null)
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
		public static Task<RefTarget> ReadRefTargetAsync(this IStorageClient store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return ReadRefTargetAsync(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}
	}
}
