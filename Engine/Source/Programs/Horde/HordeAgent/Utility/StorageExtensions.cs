// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using Build.Bazel.Remote.Execution.V2;
using EpicGames.Core;
using Google.Protobuf;
using ContentAddressableStorageClient = Build.Bazel.Remote.Execution.V2.ContentAddressableStorage.ContentAddressableStorageClient;

namespace HordeAgent.Utility
{
	static class StorageExtensions
	{
		public static async Task<byte[]> GetBulkDataAsync(this ContentAddressableStorageClient Storage, Digest Digest)
		{
			BatchReadBlobsRequest Request = new BatchReadBlobsRequest();
			Request.Digests.Add(Digest);

			BatchReadBlobsResponse Response = await Storage.BatchReadBlobsAsync(Request);
			return Response.Responses[0].Data.ToByteArray();
		}

		public static async Task<Digest> PutBulkDataAsync(this ContentAddressableStorageClient Storage, byte[] Data)
		{
			BatchUpdateBlobsRequest.Types.Request BlobRequest = new BatchUpdateBlobsRequest.Types.Request();
			BlobRequest.Data = ByteString.CopyFrom(Data);
			BlobRequest.Digest = GetDigest(Data);

			BatchUpdateBlobsRequest Request = new BatchUpdateBlobsRequest();
			Request.Requests.Add(BlobRequest);

			BatchUpdateBlobsResponse Response = await Storage.BatchUpdateBlobsAsync(Request);
			//?
			return BlobRequest.Digest;
		}

		public static async Task<T> GetProtoMessageAsync<T>(this ContentAddressableStorageClient Storage, Digest Digest) where T : class, IMessage<T>, new()
		{
			BatchReadBlobsRequest Request = new BatchReadBlobsRequest();
			Request.Digests.Add(Digest);

			BatchReadBlobsResponse Response = await Storage.BatchReadBlobsAsync(Request);

			T Item = new T();
			Item.MergeFrom(Response.Responses[0].Data);
			return Item;
		}

		public static async Task<Digest> PutProtoMessageAsync<T>(this ContentAddressableStorageClient Storage, IMessage<T> Message) where T : class, IMessage<T>
		{
			BatchUpdateBlobsRequest.Types.Request BlobRequest = new BatchUpdateBlobsRequest.Types.Request();
			BlobRequest.Data = Message.ToByteString();
			BlobRequest.Digest = GetDigest(BlobRequest.Data.ToByteArray());

			BatchUpdateBlobsRequest Request = new BatchUpdateBlobsRequest();
			Request.Requests.Add(BlobRequest);

			BatchUpdateBlobsResponse Response = await Storage.BatchUpdateBlobsAsync(Request);
			//??
			return BlobRequest.Digest;
		}

		public static Digest GetDigest(ByteString Data)
		{
			return GetDigest(Data.ToByteArray());
		}

		public static Digest GetDigest(byte[] Data)
		{
			using (SHA256 Algorithm = SHA256.Create())
			{
				byte[] Hash = Algorithm.ComputeHash(Data);
				return new Digest { Hash = StringUtils.FormatHexString(Hash), SizeBytes = Data.Length };
			}
		}
	}
}
