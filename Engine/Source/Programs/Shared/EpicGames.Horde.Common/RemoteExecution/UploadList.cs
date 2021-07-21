// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using Google.Protobuf;
using System.Collections.Concurrent;
using System.IO;
using System.Threading.Tasks;

namespace EpicGames.Horde.Common.RemoteExecution
{
	/// <summary>
	/// List of files that need to be uploaded
	/// </summary>
	public class UploadList
	{
		public ConcurrentDictionary<string, BatchUpdateBlobsRequest.Types.Request> Blobs = new ConcurrentDictionary<string, BatchUpdateBlobsRequest.Types.Request>();

		public async Task<Digest> AddAsync(ByteString ByteString)
		{
			Digest Digest = await StorageExtensions.GetDigestAsync(ByteString);
			if (!Blobs.ContainsKey(Digest.Hash))
			{
				BatchUpdateBlobsRequest.Types.Request Request = new BatchUpdateBlobsRequest.Types.Request();
				Request.Data = ByteString;
				Request.Digest = Digest;
				Blobs.TryAdd(Digest.Hash, Request);
			}

			return Digest;
		}

		public async Task<Digest> AddAsync<T>(IMessage<T> Message) where T : IMessage<T>
		{
			return await AddAsync(Message.ToByteString());
		}

		public async Task<Digest> AddFileAsync(string LocalFile)
		{
			ByteString ByteString;
			using (FileStream Stream = File.Open(LocalFile, FileMode.Open, FileAccess.Read, FileShare.Read))
			{
				ByteString = await ByteString.FromStreamAsync(Stream);
			}

			return await AddAsync(ByteString);
		}
	}
}
