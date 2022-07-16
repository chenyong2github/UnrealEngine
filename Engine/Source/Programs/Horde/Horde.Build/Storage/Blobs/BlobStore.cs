// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;

namespace Horde.Build.Storage
{
	/// <summary>
	/// Implementation of <see cref="IBlobStore"/> for AWS
	/// </summary>
	public class BlobStore : IBlobStore
	{
		readonly IStorageBackend _backend;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="backend">Backend to use for storing data</param>
		public BlobStore(IStorageBackend backend)
		{
			_backend = backend;
		}

		async Task<IBlob?> TryReadAsync(string path, CancellationToken cancellationToken = default)
		{
			ReadOnlyMemory<byte>? data = await _backend.ReadBytesAsync(path, cancellationToken);
			if (data == null)
			{
				return null;
			}
			return BlobUtils.Deserialize(data.Value);
		}

		async Task WriteAsync(string path, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			ReadOnlySequence<byte> sequence = BlobUtils.Serialize(data, references);
			await _backend.WriteBytesAsync(path, sequence.AsSingleSegment(), cancellationToken);
		}

		#region Blobs

		static string GetBlobPath(BlobId id) => $"{id}.blob";

		/// <inheritdoc/>
		public Task<IBlob?> TryReadBlobAsync(BlobId id, CancellationToken cancellationToken = default) => TryReadAsync(GetBlobPath(id), cancellationToken);

		/// <inheritdoc/>
		public async Task<BlobId> WriteBlobAsync(RefName refName, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			BlobId id = new BlobId($"{refName}/{Guid.NewGuid()}");
			await WriteAsync(GetBlobPath(id), data, references, cancellationToken);
			return id;
		}

		#endregion

		#region Refs

		static string GetRefPath(RefName name) => $"{name}.ref";

		/// <inheritdoc/>
		public async Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			await _backend.DeleteAsync(GetRefPath(name), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<bool> HasRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			return await _backend.ExistsAsync(GetRefPath(name), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<IBlob?> TryReadRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			return await TryReadAsync(GetRefPath(name), cancellationToken);
		}

		/// <inheritdoc/>
		public async Task WriteRefAsync(RefName name, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default)
		{
			await WriteAsync(GetRefPath(name), data, references, cancellationToken);
		}

		#endregion
	}
}
