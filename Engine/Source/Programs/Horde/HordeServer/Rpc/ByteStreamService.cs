// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Storage;
using HordeServer.Storage.Primitives;
using EpicGames.Core;
using Google.Bytestream;
using Google.Protobuf;
using Grpc.Core;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Globalization;

namespace HordeServer.Rpc
{
	/// <inheritdoc cref="ByteStream"/>
	class ByteStreamService : ByteStream.ByteStreamBase
	{
		/// <summary>
		/// Information about an active upload
		/// </summary>
		class UploadInfo
		{
			public long Position;
			public BlobHash Hash { get; }
			public byte[] Data { get; }

			public UploadInfo(BlobHash Hash, long Length)
			{
				this.Hash = Hash;
				this.Data = new byte[Length];
			}
		}

		/// <summary>
		/// Active uploads
		/// </summary>
		ConcurrentDictionary<string, UploadInfo> Uploads = new ConcurrentDictionary<string, UploadInfo>(StringComparer.Ordinal);

		/// <summary>
		/// Instance of the singleton storage service
		/// </summary>
		IStorageService StorageService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StorageProvider">The storage provider instance</param>
		public ByteStreamService(IStorageService StorageProvider)
		{
			this.StorageService = StorageProvider;
		}

		/// <summary>
		/// Parse the hash and size from a given resource name
		/// </summary>
		/// <param name="ResourceName">The resource name</param>
		/// <returns>THe hash and size</returns>
		static (BlobHash, long) ParseResourceName(string ResourceName)
		{
			// blobs/{hash}/{size}/foo/bar/baz.cc
			Match Match = Regex.Match(ResourceName, @"^(?:[^/]+/)?(?:uploads/[^/]+/)?blobs/([0-9a-zA-Z]+)/([0-9]+)(?:/|$)");
			if (!Match.Success)
			{
				throw new RpcException(new Status(StatusCode.InvalidArgument, "Invalid resource name"));
			}

			BlobHash Hash = BlobHash.Parse(Match.Groups[1].Value);
			long Size = long.Parse(Match.Groups[2].Value, CultureInfo.InvariantCulture);
			return (Hash, Size);
		}

		/// <inheritdoc/>
		public override async Task Read(ReadRequest Request, IServerStreamWriter<ReadResponse> ResponseStream, ServerCallContext Context)
		{
			// Get the source data
			ReadOnlyMemory<byte> SourceData;
			if (Uploads.TryGetValue(Request.ResourceName, out UploadInfo? UploadInfo))
			{
				SourceData = UploadInfo.Data.AsMemory(0, (int)UploadInfo.Position);
			}
			else
			{
				ReadOnlyMemory<byte>? Data = await StorageService.TryGetBlobAsync(ParseResourceName(Request.ResourceName).Item1);
				if (Data == null)
				{
					throw new RpcException(new Status(StatusCode.NotFound, $"Resource {Request.ResourceName} not found"));
				}
				SourceData = Data.Value;
			}

			// Get the max offset of the data
			long EndPosition = SourceData.Length;
			if (Request.ReadLimit > 0)
			{
				EndPosition = Math.Min(EndPosition, Request.ReadOffset + Request.ReadLimit);
			}

			// Return the data to the client
			for (long Position = Request.ReadOffset; Position < EndPosition; )
			{
				long ChunkSize = Math.Min(EndPosition - Position, 128 * 1024);

				ReadResponse Response = new ReadResponse();
				Response.Data = ByteString.CopyFrom(SourceData.Span.Slice((int)Position, (int)ChunkSize));
				await ResponseStream.WriteAsync(Response);

				Position += ChunkSize;
			}
		}

		/// <summary>
		/// Finds or adds an upload info object for the given resource name
		/// </summary>
		/// <param name="ResourceName">The resource name</param>
		/// <returns>Upload info object</returns>
		UploadInfo FindOrAddUploadInfo(string ResourceName)
		{
			UploadInfo? UploadInfo;
			if (!Uploads.TryGetValue(ResourceName, out UploadInfo))
			{
				(BlobHash Hash, long Size) = ParseResourceName(ResourceName);

				UploadInfo = new UploadInfo(Hash, Size);
				while (!Uploads.TryAdd(ResourceName, UploadInfo!))
				{
					UploadInfo? NewUploadInfo;
					if (Uploads.TryGetValue(ResourceName, out NewUploadInfo))
					{
						UploadInfo = NewUploadInfo;
						break;
					}
				}
			}
			return UploadInfo;
		}

		/// <inheritdoc/>
		public override async Task<WriteResponse> Write(IAsyncStreamReader<WriteRequest> RequestStream, ServerCallContext Context)
		{
			WriteResponse Response = new WriteResponse();
			if(await RequestStream.MoveNext())
			{
				WriteRequest Request = RequestStream.Current;
				string ResourceName = RequestStream.Current.ResourceName;

				UploadInfo UploadInfo = FindOrAddUploadInfo(ResourceName);
				for (; ;)
				{
					lock (UploadInfo)
					{
						if (Request.WriteOffset > UploadInfo.Position)
						{
							throw new RpcException(new Status(StatusCode.InvalidArgument, "Invalid resource name"));
						}

						Request.Data.CopyTo(UploadInfo.Data, (int)Request.WriteOffset);
						UploadInfo.Position = Request.WriteOffset + UploadInfo.Data.Length;
						Response.CommittedSize = UploadInfo.Position;
					}

					if (!await RequestStream.MoveNext())
					{
						break;
					}

					Request = RequestStream.Current;

					if(!String.IsNullOrEmpty(Request.ResourceName) && !Request.ResourceName.Equals(ResourceName, StringComparison.Ordinal))
					{
						throw new RpcException(new Status(StatusCode.InvalidArgument, "Resource name does not match original"));
					}
				}

				if (Request.FinishWrite)
				{
					await StorageService.PutBlobAsync(UploadInfo.Hash, UploadInfo.Data);
					Uploads.TryRemove(ResourceName, out _);
				}
			}
			return Response;
		}

		/// <inheritdoc/>
		public override async Task<QueryWriteStatusResponse> QueryWriteStatus(QueryWriteStatusRequest Request, ServerCallContext Context)
		{
			QueryWriteStatusResponse Response = new QueryWriteStatusResponse();
			if (Uploads.TryGetValue(Request.ResourceName, out UploadInfo? UploadInfo))
			{
				lock (UploadInfo)
				{
					Response.CommittedSize = UploadInfo.Position;
					Response.Complete = false;
					return Response;
				}
			}

			(BlobHash Hash, long Size) = ParseResourceName(Request.ResourceName);
			if (await StorageService.ShouldPutBlobAsync(Hash))
			{
				throw new RpcException(new Status(StatusCode.NotFound, "Invalid resource name"));
			}

			Response.CommittedSize = Size;
			Response.Complete = true;
			return Response;
		}
	}
}
