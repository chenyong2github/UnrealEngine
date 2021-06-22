// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using Build.Bazel.Remote.Execution.V2;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeCommon.Rpc;
using Status = Grpc.Core.Status;

using BlobStore = System.Collections.Concurrent.ConcurrentDictionary<string, Google.Protobuf.ByteString>;

namespace HordeAgentTests
{
	public class FakeCasClient : ContentAddressableStorage.ContentAddressableStorageClient
	{
		private readonly ConcurrentDictionary<string, BlobStore> BlobStoreInstances = new ConcurrentDictionary<string, BlobStore>();

		private BlobStore GetBlobStore(string InstanceName)
		{
			if (!BlobStoreInstances.ContainsKey(InstanceName))
			{
				BlobStoreInstances.TryAdd(InstanceName, new BlobStore());
			}

			return BlobStoreInstances[InstanceName];
		}
		
		public override AsyncUnaryCall<FindMissingBlobsResponse> FindMissingBlobsAsync(FindMissingBlobsRequest Request, CallOptions Options)
		{
			FindMissingBlobsResponse Res = new FindMissingBlobsResponse();
			foreach (Digest Digest in Request.BlobDigests)
			{
				if (!GetBlobStore(Request.InstanceName).ContainsKey(Digest.Hash))
				{
					Res.MissingBlobDigests.Add(Digest);
				}
			}

			return GetResponse(Res);
		}

		public override AsyncUnaryCall<BatchUpdateBlobsResponse> BatchUpdateBlobsAsync(BatchUpdateBlobsRequest Request, CallOptions Options)
		{
			BlobStore BlobStore = GetBlobStore(Request.InstanceName);
			BatchUpdateBlobsResponse Res = new BatchUpdateBlobsResponse();

			foreach (BatchUpdateBlobsRequest.Types.Request Req in Request.Requests)
			{
				BlobStore[Req.Digest.Hash] = Req.Data;
				Res.Responses.Add(new BatchUpdateBlobsResponse.Types.Response {Digest = Req.Digest, Status = Status.DefaultSuccess});
			}

			return GetResponse(Res);
		}

		public override AsyncUnaryCall<BatchReadBlobsResponse> BatchReadBlobsAsync(BatchReadBlobsRequest Request, CallOptions Options)
		{
			BatchReadBlobsResponse Response = new BatchReadBlobsResponse();
			foreach (Digest Digest in Request.Digests)
			{
				if (GetBlobStore(Request.InstanceName).TryGetValue(Digest.Hash, out ByteString? Data))
				{
					Response.Responses.Add(new BatchReadBlobsResponse.Types.Response
					{
						Data = Data,
						Digest = Digest,
						Status = Status.DefaultSuccess
					});
				}
				else
				{
					Response.Responses.Add(new BatchReadBlobsResponse.Types.Response
					{
						Data = ByteString.Empty,
						Digest = Digest,
						Status = new Status(StatusCode.NotFound, $"Digest {Digest.Hash} not found")
					});
				}
			}

			return GetResponse(Response);
		}

		public override AsyncServerStreamingCall<GetTreeResponse> GetTree(GetTreeRequest request, CallOptions options)
		{
			throw new NotImplementedException("GetTree is not implemented in FakeCasClient");
		}

		public void SetBlob(string InstanceName, string DigestHash, ByteString Data)
		{
			GetBlobStore(InstanceName)[DigestHash] = Data;
		}
		
		internal static AsyncUnaryCall<T> GetResponse<T>(T Response)
		{
			return new AsyncUnaryCall<T>(Task.FromResult(Response), null!, null!, null!, null!, null!);
		}
	}

	public class FakeActionRpcClient : ActionRpc.ActionRpcClient
	{
		internal PostActionResultRequest? ActionResultRequest;
		
		public override AsyncUnaryCall<Empty> PostActionResultAsync(PostActionResultRequest Request, CallOptions Options)
		{
			if (ActionResultRequest != null)
			{
				throw new Exception("ActionResultRequest has already been set!");
			}
			
			ActionResultRequest = Request;
			return FakeCasClient.GetResponse(new Empty());
		}
	}
}