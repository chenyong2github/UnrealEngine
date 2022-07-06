// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Interface for a blob of data for a tree node
	/// </summary>
	public interface ITreeBlob
	{
		/// <summary>
		/// Gets the data for a node with the given hash
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Data for the blob</returns>
		ValueTask<ReadOnlySequence<byte>> GetDataAsync(CancellationToken cancellationToken = default);

		/// <summary>
		/// Find the outward references for a node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>List of outward references from the blob</returns>
		ValueTask<IReadOnlyList<ITreeBlob>> GetReferencesAsync(CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Writer for trees
	/// </summary>
	public interface ITreeBlobWriter
	{
		/// <summary>
		/// Write a blob to storage
		/// </summary>
		/// <param name="data"></param>
		/// <param name="references"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public Task<ITreeBlob> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<ITreeBlob> references, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Base interface for a tree store
	/// </summary>
	public interface ITreeStore : IDisposable
	{
		/// <summary>
		/// Tests whether a tree exists with the given name
		/// </summary>
		/// <param name="id">Name of the ref used to store the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Tree reader instance</returns>
		Task<bool> HasTreeAsync(RefId id, CancellationToken cancellationToken = default);

		/// <summary>
		/// Deletes a tree with the given name
		/// </summary>
		/// <param name="id">Name of the ref used to store the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task DeleteTreeAsync(RefId id, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads the root blob from a tree in storage
		/// </summary>
		/// <param name="id">Identifier for the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<ITreeBlob> ReadTreeAsync(RefId id, CancellationToken cancellationToken = default);

		/// <summary>
		/// Creates a writer for a tree with the given name
		/// </summary>
		/// <param name="id">Name of the ref used to store the tree</param>
		/// <param name="root">Node to flush</param>
		/// <param name="flush">Whether to flush the complete tree state. If false, an implementation may choose to buffer some writes to allow packing multiple nodes together.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Tree writer instance</returns>
		Task WriteTreeAsync(RefId id, ITreeBlob root, bool flush = true, CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Interface for a typed tree store
	/// </summary>
	public interface ITreeStore<T> : ITreeStore where T : TreeNode
	{
		/// <summary>
		/// Reads a root node from the store
		/// </summary>
		/// <param name="id">Name of the tree to fetch</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Deserialized node stored with this name</returns>
		new Task<T> ReadTreeAsync(RefId id, CancellationToken cancellationToken = default);

		/// <summary>
		/// Creates a writer for a tree with the given name
		/// </summary>
		/// <param name="id">Name of the ref used to store the tree</param>
		/// <param name="root">Node to flush</param>
		/// <param name="flush">Whether to flush the complete tree state. If false, an implementation may choose to buffer some writes to allow packing multiple nodes together.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Tree writer instance</returns>
		Task WriteTreeAsync(RefId id, T root, bool flush = true, CancellationToken cancellationToken = default);
	}
}
