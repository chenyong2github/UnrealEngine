// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
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
	/// Value for a ref
	/// </summary>
	public class BundleRef
	{
		/// <summary>
		/// Location of the blob
		/// </summary>
		public BlobLocator Locator { get; }

		/// <summary>
		/// Bundle stored for this ref
		/// </summary>
		public Bundle Bundle { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleRef(BlobLocator locator, Bundle bundle)
		{
			Locator = locator;
			Bundle = bundle;
		}
	}

	/// <summary>
	/// Base interface for a low-level storage backend. Blobs added to this store are not content addressed, but referenced by <see cref="BlobLocator"/>.
	/// </summary>
	public interface IBlobStore
	{
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

		#region Refs

		/// <summary>
		/// Reads data for a ref from the store, along with the blob contents.
		/// </summary>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<BundleRef?> TryReadRefAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads data for a ref from the store
		/// </summary>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<BlobLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new ref to the store which points to a new blob
		/// </summary>
		/// <param name="name">Ref to write</param>
		/// <param name="bundle">Bundle to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the blob</returns>
		Task<BlobLocator> WriteRefAsync(RefName name, Bundle bundle, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new ref to the store
		/// </summary>
		/// <param name="name">Ref to write</param>
		/// <param name="locator">The target for the ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the blob</returns>
		Task WriteRefTargetAsync(RefName name, BlobLocator locator, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads data for a ref from the store
		/// </summary>
		/// <param name="name">The ref identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default);

		#endregion
	}

	/// <summary>
	/// Typed implementation of <see cref="IBlobStore"/> for use with dependency injection
	/// </summary>
	public interface IBlobStore<T> : IBlobStore
	{
	}

	/// <summary>
	/// Extension methods for <see cref="IBlobStore"/>
	/// </summary>
	public static class BlobStoreExtensions
	{
		class TypedBlobStore<T> : IBlobStore<T>
		{
			readonly IBlobStore _inner;

			public TypedBlobStore(IBlobStore inner) => _inner = inner;

			#region Blobs

			/// <inheritdoc/>
			public Task<Bundle> ReadBundleAsync(BlobLocator id, CancellationToken cancellationToken = default) => _inner.ReadBundleAsync(id, cancellationToken);

			/// <inheritdoc/>
			public Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default) => _inner.WriteBundleAsync(bundle, prefix, cancellationToken);

			#endregion

			#region Refs

			/// <inheritdoc/>
			public Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default) => _inner.DeleteRefAsync(name, cancellationToken);

			/// <inheritdoc/>
			public Task<BundleRef?> TryReadRefAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) => _inner.TryReadRefAsync(name, cacheTime, cancellationToken);

			/// <inheritdoc/>
			public Task<BlobLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) => _inner.TryReadRefTargetAsync(name, cacheTime, cancellationToken);

			/// <inheritdoc/>
			public Task<BlobLocator> WriteRefAsync(RefName name, Bundle bundle, CancellationToken cancellationToken = default) => _inner.WriteRefAsync(name, bundle, cancellationToken);

			/// <inheritdoc/>
			public Task WriteRefTargetAsync(RefName name, BlobLocator blobId, CancellationToken cancellationToken = default) => _inner.WriteRefTargetAsync(name, blobId, cancellationToken);

			#endregion
		}

		/// <summary>
		/// Wraps a <see cref="IBlobStore"/> interface with a type argument
		/// </summary>
		/// <param name="blobStore">Regular blob store instance</param>
		/// <returns></returns>
		public static IBlobStore<T> ForType<T>(this IBlobStore blobStore) => new TypedBlobStore<T>(blobStore);

		/// <summary>
		/// Checks if the given ref exists
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Name of the reference to look for</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the ref exists, false if it did not exist</returns>
		public static async Task<bool> HasRefAsync(this IBlobStore store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await store.TryReadRefTargetAsync(name, cacheTime, cancellationToken);
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
		public static Task<bool> HasRefAsync(this IBlobStore store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return HasRefAsync(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age of any cached ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static Task<BundleRef?> TryReadRefAsync(this IBlobStore store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return store.TryReadRefAsync(name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age of any cached ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static Task<BlobLocator> TryReadRefTargetAsync(this IBlobStore store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
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
		public static async Task<BundleRef> ReadRefAsync(this IBlobStore store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			BundleRef? bundleRef = await store.TryReadRefAsync(name, cacheTime, cancellationToken);
			if (bundleRef == null)
			{
				throw new RefNameNotFoundException(name);
			}
			return bundleRef;
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age for any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static Task<BundleRef> ReadRefAsync(this IBlobStore store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return ReadRefAsync(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="cacheTime">Minimum coherency of any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static async Task<BlobLocator> ReadRefTargetAsync(this IBlobStore store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await store.TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (!locator.IsValid())
			{
				throw new RefNameNotFoundException(name);
			}
			return locator;
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="maxAge">Maximum age for any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static Task<BlobLocator> ReadRefTargetAsync(this IBlobStore store, RefName name, TimeSpan maxAge, CancellationToken cancellationToken = default)
		{
			return ReadRefTargetAsync(store, name, DateTime.UtcNow - maxAge, cancellationToken);
		}
	}
}
