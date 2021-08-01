// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeServer.Utilities;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;

	class RefTableRpc : RefTable.RefTableBase
	{
		IRefCollection RefCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RefCollection"></param>
		public RefTableRpc(IRefCollection RefCollection)
		{
			this.RefCollection = RefCollection;
		}

		/// <inheritdoc/>
		public override async Task<SetRefResponse> Set(SetRefRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			BucketId BucketId = new BucketId(Request.BucketId);
			List<IoHash> MissingHashes = await RefCollection.SetAsync(NamespaceId, BucketId, Request.Name, Request.Object);

			return new SetRefResponse(MissingHashes);
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

			return new GetRefResponse(Ref.Value, Ref.LastAccessTime, Ref.Finalized);
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
