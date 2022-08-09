// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Interface for a node within a tree, represented by an opaque blob of data
	/// </summary>
	public interface ITreeBlob
	{
		/// <summary>
		/// Data for the node
		/// </summary>
		ReadOnlySequence<byte> Data { get; }

		/// <summary>
		/// References to other tree blobs
		/// </summary>
		IReadOnlyList<ITreeBlobRef> Refs { get; }
	}

	/// <summary>
	/// Reference to a <see cref="ITreeBlob"/>
	/// </summary>
	public interface ITreeBlobRef
	{
		/// <summary>
		/// Gets the target of a reference
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The referenced node</returns>
		ValueTask<ITreeBlob> GetTargetAsync(CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Base interface for a tree store
	/// </summary>
	public interface ITreeStore : IDisposable
	{
		/// <summary>
		/// Creates a new context for reading trees.
		/// </summary>
		/// <returns>New context instance</returns>
		ITreeWriter CreateTreeWriter(RefName name);

		/// <summary>
		/// Deletes a tree with the given name
		/// </summary>
		/// <param name="name">Name of the ref used to store the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task DeleteTreeAsync(RefName name, CancellationToken cancellationToken = default);

		/// <summary>
		/// Tests whether a tree exists with the given name
		/// </summary>
		/// <param name="name">Name of the ref used to store the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Tree reader instance</returns>
		Task<bool> HasTreeAsync(RefName name, CancellationToken cancellationToken = default);

		/// <summary>
		/// Attempts to read a tree with the given name
		/// </summary>
		/// <param name="name">Name of the ref used to store the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Reference to the root of the tree. Null if the ref does not exist.</returns>
		Task<ITreeBlob?> TryReadTreeAsync(RefName name, CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Interface for a typed tree store
	/// </summary>
	public interface ITreeStore<T> : ITreeStore
	{
	}

	/// <summary>
	/// Allows incrementally writing new trees
	/// </summary>
	public interface ITreeWriter
	{
		/// <summary>
		/// Adds a new node to this tree
		/// </summary>
		/// <param name="data">Data for the node</param>
		/// <param name="refs">References to other nodes</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Reference to the node that was added</returns>
		Task<ITreeBlobRef> WriteNodeAsync(ReadOnlySequence<byte> data, IReadOnlyList<ITreeBlobRef> refs, CancellationToken cancellationToken = default);

		/// <summary>
		/// Flush the tree with the given root.
		/// </summary>
		/// <param name="root">The root node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task FlushAsync(ITreeBlob root, CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Extension methods for tree store instances
	/// </summary>
	public static class TreeStoreExtensions
	{
		sealed class TypedTreeStore<T> : ITreeStore<T>
		{
			readonly ITreeStore _inner;

			public TypedTreeStore(ITreeStore inner)
			{
				_inner = inner;
			}

			/// <inheritdoc/>
			public ITreeWriter CreateTreeWriter(RefName name) => _inner.CreateTreeWriter(name);

			/// <inheritdoc/>
			public Task DeleteTreeAsync(RefName name, CancellationToken cancellationToken = default) => _inner.DeleteTreeAsync(name, cancellationToken);

			/// <inheritdoc/>
			public void Dispose() => _inner.Dispose();

			/// <inheritdoc/>
			public Task<bool> HasTreeAsync(RefName name, CancellationToken cancellationToken = default) => _inner.HasTreeAsync(name, cancellationToken);

			/// <inheritdoc/>
			public Task<ITreeBlob?> TryReadTreeAsync(RefName name, CancellationToken cancellationToken = default) => _inner.TryReadTreeAsync(name, cancellationToken);
		}

		/// <summary>
		/// Wraps an <see cref="ITreeStore"/> interface with a type for dependency injection
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="store">The instance to wrap</param>
		/// <returns>Wrapped instance of the tree store</returns>
		public static ITreeStore<T> ForType<T>(this ITreeStore store)
		{
			return new TypedTreeStore<T>(store);
		}
	}
}
