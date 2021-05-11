// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using Blake3;
using Build.Bazel.Remote.Execution.V2;
using EpicGames.Core;
using Google.Protobuf;
using ContentAddressableStorageClient = Build.Bazel.Remote.Execution.V2.ContentAddressableStorage.ContentAddressableStorageClient;

namespace HordeAgent.Utility
{
	static class StorageExtensions
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
			BlobRequest.Digest = GetDigest(Data);

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
			if (Response.Responses[0].Status.Code != (int) Google.Rpc.Code.Ok)
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
			BlobRequest.Digest = GetDigest(BlobRequest.Data.ToByteArray());

			BatchUpdateBlobsRequest Request = new BatchUpdateBlobsRequest();
			Request.InstanceName = InstanceName;
			Request.Requests.Add(BlobRequest);

			BatchUpdateBlobsResponse Response = await Storage.BatchUpdateBlobsAsync(Request);
			//??
			return BlobRequest.Digest;
		}
		
		public static Digest GetDigest(ByteString Data)
		{
			return GetDigest(Data.ToByteArray(), DigestFunction.Types.Value.Epiciohash);
		}

		public static Digest GetDigest(byte[] Data)
		{
			return GetDigest(Data, DigestFunction.Types.Value.Epiciohash);
		}

		public static Digest GetDigest(ByteString Data, DigestFunction.Types.Value DigestFunction)
		{
			return GetDigest(Data.ToByteArray(), DigestFunction);
		}

		public static Digest GetDigest(byte[] Data, DigestFunction.Types.Value DigestFunction)
		{
			switch (DigestFunction)
			{
				case Build.Bazel.Remote.Execution.V2.DigestFunction.Types.Value.Sha256: return GetDigestSha256(Data);
				case Build.Bazel.Remote.Execution.V2.DigestFunction.Types.Value.Epiciohash: return GetDigestIoHash(Data);
				default: throw new Exception("Unknown digest function " + DigestFunction);
			}
		}
		
		public static Digest GetDigestSha256(byte[] Data)
		{
			using (SHA256 Algorithm = SHA256.Create())
			{
				byte[] Hash = Algorithm.ComputeHash(Data);
				return new Digest { Hash = StringUtils.FormatHexString(Hash), SizeBytes = Data.Length };
			}
		}
		
		public static Digest GetDigestIoHash(byte[] Data)
		{
			using Hasher Hasher = Hasher.New();
			const int HashLength = 20;
			Hasher.UpdateWithJoin(Data);
			Hash Blake3Hash = Hasher.Finalize();
            
			// we only keep the first 20 bytes of the Blake3 hash
			Span<byte> Hash = Blake3Hash.AsSpan().Slice(0, HashLength);
			//return new Digest { Hash = StringUtils.FormatHexString(Hash), SizeBytes = HashLength };
			return new Digest { Hash = StringUtils.FormatHexString(Hash).ToUpper(), SizeBytes = HashLength };
		}
	}
}
