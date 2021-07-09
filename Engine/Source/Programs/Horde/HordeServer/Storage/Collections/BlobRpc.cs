// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Common.Storage;
using Google.Protobuf;
using Grpc.Core;
using HordeServer.Utilities;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;

	class BlobRpc : BlobService.BlobServiceBase
	{
		IBlobCollection BlobCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="BlobCollection"></param>
		public BlobRpc(IBlobCollection BlobCollection)
		{
			this.BlobCollection = BlobCollection;
		}

		/// <inheritdoc/>
		public override async Task<AddBlobResponse> Add(AddBlobRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			IoHash Hash = new IoHash(Request.Hash.ToByteArray());
			await BlobCollection.WriteBytesAsync(NamespaceId, Hash, Request.Data.ToByteArray());
			return new AddBlobResponse();
		}

		/// <inheritdoc/>
		public override async Task<GetBlobResponse> Get(GetBlobRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			IoHash Hash = new IoHash(Request.Hash.ToByteArray());

			Stream? Stream = await BlobCollection.ReadAsync(NamespaceId, Hash);
			if(Stream == null)
			{
				throw new RpcException(new Status(StatusCode.NotFound, "Blob not found"));
			}

			GetBlobResponse Response = new GetBlobResponse();
			Response.Data = await ByteString.FromStreamAsync(Stream);
			return Response;
		}

		/// <inheritdoc/>
		public override async Task<ExistsResponse> Exists(ExistsRequest Request, ServerCallContext Context)
		{
			NamespaceId NamespaceId = new NamespaceId(Request.NamespaceId);
			List<IoHash> Hashes = ParseIoHashList(Request.Hashes);

			List<IoHash> Results = await BlobCollection.ExistsAsync(NamespaceId, Hashes);

			ExistsResponse Response = new ExistsResponse();
			Response.Hashes = BuildIoHashList(Results);
			return Response;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ByteString"></param>
		/// <returns></returns>
		public static List<IoHash> ParseIoHashList(ByteString ByteString)
		{
			List<IoHash> Hashes = new List<IoHash>();
			for (int Offset = 0; Offset < ByteString.Length; Offset += IoHash.NumBytes)
			{
				Hashes.Add(new IoHash(ByteString.Span.Slice(Offset, IoHash.NumBytes).ToArray()));
			}
			return Hashes;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="Hashes"></param>
		/// <returns></returns>
		public static ByteString BuildIoHashList(List<IoHash> Hashes)
		{
			byte[] Data = new byte[Hashes.Count * IoHash.NumBytes];

			Span<byte> Output = Data;
			foreach (IoHash Hash in Hashes)
			{
				Hash.Span.CopyTo(Output);
				Output = Output.Slice(IoHash.NumBytes);
			}

			return ByteString.CopyFrom(Data);
		}
	}
}
