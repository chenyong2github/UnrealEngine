// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Impl
{
	/// <summary>
	/// Implementation of <see cref="IStorageClient"/> which writes directly to memory for testing. Not intended for production use.
	/// </summary>
	public class MemoryStorageClient : IStorageClient
	{
		class Ref : IRef
		{
			public NamespaceId NamespaceId { get; set; }
			public BucketId BucketId { get; set; }
			public RefId RefId { get; set; }
			public CbObject Value { get; set; }

			public Ref(NamespaceId namespaceId, BucketId bucketId, RefId refId, CbObject value)
			{
				NamespaceId = namespaceId;
				BucketId = bucketId;
				RefId = refId;
				Value = value;
			}
		}

		/// <inheritdoc/>
		public Dictionary<(NamespaceId, IoHash), ReadOnlyMemory<byte>> Blobs { get; } = new Dictionary<(NamespaceId, IoHash), ReadOnlyMemory<byte>>();

		/// <inheritdoc/>
		public Dictionary<(NamespaceId, BucketId, RefId), IRef> Refs { get; } = new Dictionary<(NamespaceId, BucketId, RefId), IRef>();

		/// <inheritdoc/>
		public Task<Stream> ReadBlobAsync(NamespaceId namespaceId, IoHash hash, CancellationToken cancellationToken = default)
		{
			ReadOnlyMemory<byte> data = Blobs[(namespaceId, hash)];
			return Task.FromResult<Stream>(new ReadOnlyMemoryStream(data));
		}

		/// <inheritdoc/>
		public async Task WriteBlobAsync(NamespaceId namespaceId, IoHash hash, Stream stream, CancellationToken cancellationToken = default)
		{
			using (MemoryStream memoryStream = new MemoryStream())
			{
				await stream.CopyToAsync(memoryStream, cancellationToken);
				Blobs[(namespaceId, hash)] = memoryStream.ToArray();
			}
		}

		/// <inheritdoc/>
		public async Task<IoHash> WriteBlobAsync(NamespaceId namespaceId, Stream stream, CancellationToken cancellationToken = default)
		{
			using (MemoryStream memoryStream = new MemoryStream())
			{
				await stream.CopyToAsync(memoryStream, cancellationToken);

				byte[] data = memoryStream.ToArray();
				IoHash hash = IoHash.Compute(data);

				Blobs[(namespaceId, hash)] = data;
				return hash;
			}
		}

		/// <inheritdoc/>
		public Task<bool> HasBlobAsync(NamespaceId namespaceId, IoHash hash, CancellationToken cancellationToken = default)
		{
			return Task.FromResult(Blobs.ContainsKey((namespaceId, hash)));
		}

		/// <inheritdoc/>
		public Task<HashSet<IoHash>> FindMissingBlobsAsync(NamespaceId namespaceId, HashSet<IoHash> hashes, CancellationToken cancellationToken)
		{
			return Task.FromResult(hashes.Where(x => !Blobs.ContainsKey((namespaceId, x))).ToHashSet());
		}

		/// <inheritdoc/>
		public Task<bool> DeleteRefAsync(NamespaceId namespaceId, BucketId bucketId, RefId refId, CancellationToken cancellationToken = default)
		{
			return Task.FromResult(Refs.Remove((namespaceId, bucketId, refId)));
		}

		/// <inheritdoc/>
		public Task<List<RefId>> FindMissingRefsAsync(NamespaceId namespaceId, BucketId bucketId, List<RefId> refIds, CancellationToken cancellationToken = default)
		{
			return Task.FromResult(refIds.Where(x => !Refs.ContainsKey((namespaceId, bucketId, x))).ToList());
		}

		/// <inheritdoc/>
		public Task<IRef> GetRefAsync(NamespaceId namespaceId, BucketId bucketId, RefId refId, CancellationToken cancellationToken = default)
		{
			return Task.FromResult(Refs[(namespaceId, bucketId, refId)]);
		}

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(NamespaceId namespaceId, BucketId bucketId, RefId refId, CancellationToken cancellationToken = default)
		{
			return Task.FromResult(Refs.ContainsKey((namespaceId, bucketId, refId)));
		}

		/// <inheritdoc/>
		public Task<List<IoHash>> TryFinalizeRefAsync(NamespaceId namespaceId, BucketId bucketId, RefId refId, IoHash hash, CancellationToken cancellationToken = default)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public Task<List<IoHash>> TrySetRefAsync(NamespaceId namespaceId, BucketId bucketId, RefId refId, CbObject value, CancellationToken cancellationToken = default)
		{
			Refs[(namespaceId, bucketId, refId)] = new Ref(namespaceId, bucketId, refId, value);
			return Task.FromResult(new List<IoHash>());
		}
	}
}
