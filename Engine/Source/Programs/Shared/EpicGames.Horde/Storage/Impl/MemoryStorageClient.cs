// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
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

			public Ref(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value)
			{
				this.NamespaceId = NamespaceId;
				this.BucketId = BucketId;
				this.RefId = RefId;
				this.Value = Value;
			}
		}

		/// <inheritdoc/>
		public Dictionary<(NamespaceId, IoHash), ReadOnlyMemory<byte>> Blobs { get; } = new Dictionary<(NamespaceId, IoHash), ReadOnlyMemory<byte>>();

		/// <inheritdoc/>
		public Dictionary<(NamespaceId, BucketId, RefId), IRef> Refs { get; } = new Dictionary<(NamespaceId, BucketId, RefId), IRef>();

		/// <inheritdoc/>
		public Task<Stream> ReadBlobAsync(NamespaceId NamespaceId, IoHash Hash, CancellationToken CancellationToken = default)
		{
			ReadOnlyMemory<byte> Data = Blobs[(NamespaceId, Hash)];
			return Task.FromResult<Stream>(new ReadOnlyMemoryStream(Data));
		}

		/// <inheritdoc/>
		public async Task WriteBlobAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream, CancellationToken CancellationToken = default)
		{
			using (MemoryStream MemoryStream = new MemoryStream())
			{
				await Stream.CopyToAsync(MemoryStream);
				Blobs[(NamespaceId, Hash)] = MemoryStream.ToArray();
			}
		}

		/// <inheritdoc/>
		public Task<bool> HasBlobAsync(NamespaceId NamespaceId, IoHash Hash, CancellationToken CancellationToken = default)
		{
			return Task.FromResult(Blobs.ContainsKey((NamespaceId, Hash)));
		}

		/// <inheritdoc/>
		public Task<bool> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default)
		{
			return Task.FromResult(Refs.Remove((NamespaceId, BucketId, RefId)));
		}

		/// <inheritdoc/>
		public Task<List<RefId>> FindMissingRefsAsync(NamespaceId NamespaceId, BucketId BucketId, List<RefId> RefIds, CancellationToken CancellationToken = default)
		{
			return Task.FromResult(RefIds.Where(x => !Refs.ContainsKey((NamespaceId, BucketId, x))).ToList());
		}

		/// <inheritdoc/>
		public Task<IRef> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default)
		{
			return Task.FromResult(Refs[(NamespaceId, BucketId, RefId)]);
		}

		/// <inheritdoc/>
		public Task<bool> HasRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default)
		{
			return Task.FromResult(Refs.ContainsKey((NamespaceId, BucketId, RefId)));
		}

		/// <inheritdoc/>
		public Task<List<IoHash>> TryFinalizeRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, IoHash Hash, CancellationToken CancellationToken = default)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public Task<List<IoHash>> TrySetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value, CancellationToken CancellationToken = default)
		{
			Refs[(NamespaceId, BucketId, RefId)] = new Ref(NamespaceId, BucketId, RefId, Value);
			return Task.FromResult(new List<IoHash>());
		}
	}
}
