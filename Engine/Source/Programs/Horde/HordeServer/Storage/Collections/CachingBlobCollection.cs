// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Implements an in-memory cache over a blob collection
	/// </summary>
	class CachingBlobCollection : IBlobCollection, IDisposable
	{
		IBlobCollection Inner;
		MemoryCache Cache;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The inner blob collection instance</param>
		/// <param name="CacheSize">The cache size</param>
		public CachingBlobCollection(IBlobCollection Inner, long CacheSize)
		{
			this.Inner = Inner;
			this.Cache = new MemoryCache(new MemoryCacheOptions { SizeLimit = CacheSize });
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Cache.Dispose();
		}

		/// <inheritdoc/>
		public Task<Stream?> TryReadStreamAsync(NamespaceId NamespaceId, IoHash Hash) => Inner.TryReadStreamAsync(NamespaceId, Hash);

		/// <inheritdoc/>
		public async Task<ReadOnlyMemory<byte>?> TryReadBytesAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			ReadOnlyMemory<byte> Data;
			if(Cache.TryGetValue(Hash, out Data))
			{
				return Data;
			}

			ReadOnlyMemory<byte>? Result = await Inner.TryReadBytesAsync(NamespaceId, Hash);
			if (Result != null)
			{
				using (ICacheEntry Entry = Cache.CreateEntry(Hash))
				{
					Entry.SetSize(Result.Value.Length);
					Entry.SetValue(Result.Value);
				}
			}

			return Result;
		}

		/// <inheritdoc/>
		public Task WriteStreamAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream) => Inner.WriteStreamAsync(NamespaceId, Hash, Stream);

		/// <inheritdoc/>
		public Task WriteBytesAsync(NamespaceId NamespaceId, IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			ReadOnlyMemory<byte> BytesCopy = Data.ToArray();
			using (ICacheEntry Entry = Cache.CreateEntry(Hash))
			{
				Entry.SetSize(BytesCopy.Length);
				Entry.SetValue(BytesCopy);
			}
			return Inner.WriteBytesAsync(NamespaceId, Hash, BytesCopy);
		}

		/// <inheritdoc/>
		public Task<List<IoHash>> ExistsAsync(NamespaceId NamespaceId, IEnumerable<IoHash> Hashes) => Inner.ExistsAsync(NamespaceId, Hashes);
	}
}
