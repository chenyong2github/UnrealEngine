// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using EpicGames.Core;
using Google.Protobuf;
using System;
using System.Security.Cryptography;
using System.Threading.Tasks;
using ContentAddressableStorageClient = Build.Bazel.Remote.Execution.V2.ContentAddressableStorage.ContentAddressableStorageClient;
using Digest = Build.Bazel.Remote.Execution.V2.Digest;

namespace EpicGames.Horde.Common.RemoteExecution
{
	public static class StorageExtensions
	{
		public static async Task<byte[]> GetBulkDataAsync(this ContentAddressableStorageClient Storage, string InstanceName, Digest Digest)
		{
			BatchReadBlobsRequest Request = new BatchReadBlobsRequest();
			Request.InstanceName = InstanceName;
			Request.Digests.Add(Digest);

			BatchReadBlobsResponse Response = await Storage.BatchReadBlobsAsync(Request);
			return Response.Responses[0].Data.ToByteArray();
		}

		public static async Task<Digest> PutBulkDataAsync(this ContentAddressableStorageClient Storage, string InstanceName, byte[] Data)
		{
			BatchUpdateBlobsRequest.Types.Request BlobRequest = new BatchUpdateBlobsRequest.Types.Request();
			BlobRequest.Data = ByteString.CopyFrom(Data);
			BlobRequest.Digest = await GetDigestAsync(Data);

			BatchUpdateBlobsRequest Request = new BatchUpdateBlobsRequest();
			Request.InstanceName = InstanceName;
			Request.Requests.Add(BlobRequest);

			BatchUpdateBlobsResponse Response = await Storage.BatchUpdateBlobsAsync(Request);
			//?
			return BlobRequest.Digest;
		}

		public static async Task<T> GetProtoMessageAsync<T>(this ContentAddressableStorageClient Storage, string InstanceName, Digest Digest) where T : class, IMessage<T>, new()
		{
			BatchReadBlobsRequest Request = new BatchReadBlobsRequest();
			Request.InstanceName = InstanceName;
			Request.Digests.Add(Digest);

			BatchReadBlobsResponse Response = await Storage.BatchReadBlobsAsync(Request);
			if (Response.Responses[0].Status.Code != (int)Google.Rpc.Code.Ok)
			{
				throw new Exception($"Failed getting digest {Digest}. Code={Response.Responses[0].Status.Code} Message={Response.Responses[0].Status.Message}");
			}

			T Item = new T();
			Item.MergeFrom(Response.Responses[0].Data);
			return Item;
		}

		public static async Task<Digest> PutProtoMessageAsync<T>(this ContentAddressableStorageClient Storage, string InstanceName, IMessage<T> Message) where T : class, IMessage<T>
		{
			BatchUpdateBlobsRequest.Types.Request BlobRequest = new BatchUpdateBlobsRequest.Types.Request();
			BlobRequest.Data = Message.ToByteString();
			BlobRequest.Digest = await GetDigestAsync(BlobRequest.Data);

			BatchUpdateBlobsRequest Request = new BatchUpdateBlobsRequest();
			Request.InstanceName = InstanceName;
			Request.Requests.Add(BlobRequest);

			BatchUpdateBlobsResponse Response = await Storage.BatchUpdateBlobsAsync(Request);
			//??
			return BlobRequest.Digest;
		}

		public static async Task<Digest> GetDigestAsync(ByteString Data)
		{
			return await GetDigestAsync(Data.ToByteArray(), DigestFunction.Types.Value.Epiciohash);
		}

		public static async Task<Digest> GetDigestAsync(byte[] Data)
		{
			return await GetDigestAsync(Data, DigestFunction.Types.Value.Epiciohash);
		}

		public static async Task<Digest> GetDigestAsync(ByteString Data, DigestFunction.Types.Value DigestFunction)
		{
			return await GetDigestAsync(Data.ToByteArray(), DigestFunction);
		}

		public static async Task<Digest> GetDigestAsync(byte[] Data, DigestFunction.Types.Value DigestFunction)
		{
			switch (DigestFunction)
			{
				case Build.Bazel.Remote.Execution.V2.DigestFunction.Types.Value.Sha256: return await GetDigestSha256Async(Data);
				case Build.Bazel.Remote.Execution.V2.DigestFunction.Types.Value.Epiciohash: return await GetDigestIoHashAsync(Data);
				default: throw new NotImplementedException($"Unsupported digest function {DigestFunction}");
			}
		}

		public static async Task<Digest> GetDigestSha256Async(byte[] Data)
		{
			string Hash = await Task.Run(() =>
			{
				byte[] Result;
				using (SHA256 Algorithm = SHA256.Create())
				{
					Result = Algorithm.ComputeHash(Data);
				}
				return StringUtils.FormatHexString(Result);
			});
			return new Digest { Hash = Hash, SizeBytes = Data.Length };
		}

		public static async Task<Digest> GetDigestIoHashAsync(byte[] Data)
		{
			string Hash = await Task.Run(() =>
			{
				IoHash Result = IoHash.Compute(Data.AsSpan());
				return StringUtils.FormatHexString(Result.Span).ToUpper();
			});
			return new Digest { Hash = Hash, SizeBytes = Data.Length };
		}
	}
}
