// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using HordeServer.Storage;
using HordeServer.Utility;
using EpicGames.Core;
using Google.Protobuf;
using Google.Rpc;
using Grpc.Core;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;

using Status = Google.Rpc.Status;

namespace HordeServer.Rpc
{
	/// <inheritdoc cref="ContentAddressableStorage"/>
	class ContentStorageService : ContentAddressableStorage.ContentAddressableStorageBase
	{
		/// <summary>
		/// Storage service singleton
		/// </summary>
		IStorageService StorageService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StorageService"></param>
		public ContentStorageService(IStorageService StorageService)
		{
			this.StorageService = StorageService;
		}

		/// <inheritdoc/>
		public override async Task<BatchReadBlobsResponse> BatchReadBlobs(BatchReadBlobsRequest Request, ServerCallContext Context)
		{
			BatchReadBlobsResponse Response = new BatchReadBlobsResponse();
			foreach (Digest ItemDigest in Request.Digests)
			{
				ReadOnlyMemory<byte>? Data = await StorageService.TryGetBlobAsync(ItemDigest.ToHashValue());

				BatchReadBlobsResponse.Types.Response Item = new BatchReadBlobsResponse.Types.Response();
				Item.Digest = ItemDigest;
				if (Data == null)
				{
					Item.Data = ByteString.Empty;
					Item.Status = new Status { Code = (int)Code.NotFound };
				}
				else
				{
					Item.Data = ByteString.CopyFrom(Data.Value.Span);
					Item.Status = new Status { Code = (int)Code.Ok };
				}
				Response.Responses.Add(Item);
			}
			return Response;
		}

		/// <inheritdoc/>
		public override async Task<BatchUpdateBlobsResponse> BatchUpdateBlobs(BatchUpdateBlobsRequest Request, ServerCallContext Context)
		{
			BatchUpdateBlobsResponse Response = new BatchUpdateBlobsResponse();
			foreach (BatchUpdateBlobsRequest.Types.Request ItemRequest in Request.Requests)
			{
				await StorageService.PutBlobAsync(ItemRequest.Digest.ToHashValue(), ItemRequest.Data.ToByteArray());

				BatchUpdateBlobsResponse.Types.Response ItemResponse = new BatchUpdateBlobsResponse.Types.Response();
				ItemResponse.Digest = ItemRequest.Digest;
				ItemResponse.Status = new Status { Code = (int)Code.Ok };
				Response.Responses.Add(ItemResponse);
			}
			return Response;
		}

		/// <inheritdoc/>
		public override async Task<FindMissingBlobsResponse> FindMissingBlobs(FindMissingBlobsRequest Request, ServerCallContext Context)
		{
			FindMissingBlobsResponse Response = new FindMissingBlobsResponse();
			foreach (Digest BlobDigest in Request.BlobDigests)
			{
				if (await StorageService.ShouldPutBlobAsync(BlobDigest.ToHashValue()))
				{
					Response.MissingBlobDigests.Add(BlobDigest);
				}
			}
			return Response;
		}

		/// <inheritdoc/>
		public override Task GetTree(GetTreeRequest request, IServerStreamWriter<GetTreeResponse> ResponseStream, ServerCallContext Context)
		{
			throw new NotImplementedException();
		}
	}
}
