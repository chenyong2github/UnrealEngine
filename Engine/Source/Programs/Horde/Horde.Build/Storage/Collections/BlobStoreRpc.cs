// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Horde.Storage;
using Google.Protobuf;
using Grpc.Core;
using HordeServer.Utilities;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;

	class BlobStoreRpc : BlobStore.BlobStoreBase
	{
		IBlobCollection BlobCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="BlobCollection"></param>
		public BlobStoreRpc(IBlobCollection BlobCollection)
		{
			this.BlobCollection = BlobCollection;
		}

		/// <inheritdoc/>
		public override async Task<AddBlobsResponse> Add(AddBlobsRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			foreach (AddBlobRequest Blob in Request.Blobs)
			{
				await BlobCollection.WriteBytesAsync(NamespaceId, Blob.Hash, Blob.Data.ToByteArray());
			}
			return new AddBlobsResponse();
		}

		/// <inheritdoc/>
		public override async Task<GetBlobsResponse> Get(GetBlobsRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);

			List<Task<ReadOnlyMemory<byte>?>> Tasks = new List<Task<ReadOnlyMemory<byte>?>>();
			foreach (IoHash Hash in Request.Blobs)
			{
				Tasks.Add(Task.Run(() => BlobCollection.TryReadBytesAsync(NamespaceId, Hash)));
			}

			GetBlobsResponse Response = new GetBlobsResponse();
			for (int Idx = 0; Idx < Request.Blobs.Count; Idx++)
			{
				ReadOnlyMemory<byte>? Data = await Tasks[Idx];

				GetBlobResponse Blob = new GetBlobResponse();
				if (Data != null)
				{
					Blob.Data = ByteString.CopyFrom(Data.Value.Span);
				}
				Blob.Exists = Data != null;
				Response.Blobs.Add(Blob);
			}
			return Response;
		}

		/// <inheritdoc/>
		public override async Task<FindMissingBlobsResponse> FindMissing(FindMissingBlobsRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			IEnumerable<IoHash> Hashes = Request.Blobs.Select(x => (IoHash)x);

			List<IoHash> ExistingHashes = await BlobCollection.ExistsAsync(NamespaceId, Hashes);

			FindMissingBlobsResponse Response = new FindMissingBlobsResponse();
			Response.Blobs.Add(Hashes.Except(ExistingHashes).Select(x => (IoHashWrapper)x));
			return Response;
		}
	}
}
