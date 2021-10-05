// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;

	class ObjectCollection : IObjectCollection
	{
		IBlobCollection BlobCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="BlobCollection"></param>
		public ObjectCollection(IBlobCollection BlobCollection)
		{
			this.BlobCollection = BlobCollection;
		}

		/// <inheritdoc/>
		public async Task AddAsync(NamespaceId NsId, IoHash Hash, CbObject Obj)
		{
			ReadOnlyMemory<byte> Memory = Obj.GetView();
			await BlobCollection.WriteBytesAsync(NsId, Hash, Memory);
		}

		/// <inheritdoc/>
		public async Task<CbObject?> GetAsync(NamespaceId NsId, IoHash Hash)
		{
			ReadOnlyMemory<byte>? Data = await BlobCollection.TryReadBytesAsync(NsId, Hash);
			if(Data == null)
			{
				return null;
			}
			return new CbObject(Data.Value);
		}

		/// <inheritdoc/>
		public Task<List<IoHash>> ExistsAsync(NamespaceId NsId, IEnumerable<IoHash> Hashes)
		{
			return BlobCollection.ExistsAsync(NsId, Hashes);
		}
	}

	/// <summary>
	/// Extension methods for <see cref="IObjectCollection"/>
	/// </summary>
	public static class ObjectCollectionExtensions
	{
		/// <summary>
		/// Test whether a single object exists
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<bool> ExistsAsync(this IObjectCollection Collection, NamespaceId NamespaceId, IoHash Hash)
		{
			List<IoHash> Hashes = await Collection.ExistsAsync(NamespaceId, new[] { Hash });
			return Hashes.Count > 0;
		}
	}
}
