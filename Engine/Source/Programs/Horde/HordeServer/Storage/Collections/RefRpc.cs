// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Common.Storage;
using EpicGames.Serialization;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeServer.Utilities;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;

	class RefRpc : RefService.RefServiceBase
	{
		IRefCollection RefCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RefCollection"></param>
		public RefRpc(IRefCollection RefCollection)
		{
			this.RefCollection = RefCollection;
		}

		/// <inheritdoc/>
		public override async Task<SetRefResponse> Set(SetRefRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			BucketId BucketId = new BucketId(Request.BucketId);
			List<IoHash> MissingHashes = await RefCollection.SetAsync(NamespaceId, BucketId, Request.Name, new CbObject(Request.Value.ToByteArray()));

			SetRefResponse Response = new SetRefResponse();
			Response.MissingHashes = BlobRpc.BuildIoHashList(MissingHashes);
			return Response;
		}

		/// <inheritdoc/>
		public override async Task<GetRefResponse> Get(GetRefRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			BucketId BucketId = new BucketId(Request.BucketId);

			IRef? Ref = await RefCollection.GetAsync(NamespaceId, BucketId, Request.Name);
			if(Ref == null)
			{
				throw new RpcException(new Status(StatusCode.NotFound, $"Unable to find ref {NamespaceId}/{BucketId}/{Request.Name}"));
			}

			GetRefResponse Response = new GetRefResponse();
			Response.LastAccessTime = Timestamp.FromDateTime(Ref.LastAccessTime);
			Response.Value = ByteString.CopyFrom(Ref.Value.GetView().Span);
			Response.Finalized = Ref.Finalized;
			return Response;
		}

		/// <inheritdoc/>
		public override async Task<TouchResponse> Touch(TouchRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			BucketId BucketId = new BucketId(Request.BucketId);

			if (!await RefCollection.TouchAsync(NamespaceId, BucketId, Request.Name))
			{
				throw new RpcException(new Status(StatusCode.NotFound, $"Unable to find ref {NamespaceId}/{BucketId}/{Request.Name}"));
			}

			return new TouchResponse();
		}

		/// <inheritdoc/>
		public override async Task<DeleteResponse> Delete(DeleteRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			BucketId BucketId = new BucketId(Request.BucketId);

			if (!await RefCollection.DeleteAsync(NamespaceId, BucketId, Request.Name))
			{
				throw new RpcException(new Status(StatusCode.NotFound, $"Unable to find ref {NamespaceId}/{BucketId}/{Request.Name}"));
			}

			return new DeleteResponse();
		}
	}
}
