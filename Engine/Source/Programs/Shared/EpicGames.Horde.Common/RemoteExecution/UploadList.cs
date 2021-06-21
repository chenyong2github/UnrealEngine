// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using Google.Protobuf;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;

namespace EpicGames.Horde.Common.RemoteExecution
{
	/// <summary>
	/// List of files that need to be uploaded
	/// </summary>
	public class UploadList
	{
		public Dictionary<string, BatchUpdateBlobsRequest.Types.Request> Blobs = new Dictionary<string, BatchUpdateBlobsRequest.Types.Request>();

		public Digest Add<T>(IMessage<T> Message) where T : IMessage<T>
		{
			ByteString ByteString = Message.ToByteString();

			Digest Digest = StorageExtensions.GetDigest(ByteString);
			if (!Blobs.ContainsKey(Digest.Hash))
			{
				BatchUpdateBlobsRequest.Types.Request Request = new BatchUpdateBlobsRequest.Types.Request();
				Request.Data = ByteString;
				Request.Digest = Digest;
				Blobs.Add(Digest.Hash, Request);
			}
			return Digest;
		}

		public async Task<Digest> AddFileAsync(string LocalFile)
		{
			BatchUpdateBlobsRequest.Types.Request Content = new BatchUpdateBlobsRequest.Types.Request();
			using (FileStream Stream = File.Open(LocalFile, FileMode.Open, FileAccess.Read, FileShare.Read))
			{
				Content.Data = await ByteString.FromStreamAsync(Stream);
				Content.Digest = StorageExtensions.GetDigest(Content.Data);
			}
			Blobs[Content.Digest.Hash] = Content;
			return Content.Digest;
		}
	}
}
