// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Google.Protobuf;
using Grpc.Core;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Exception thrown when a blob is not found in the object store
	/// </summary>
	public sealed class BlobNotFoundException : Exception
	{
		/// <summary>
		/// Hash of the missing blob
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Hash"></param>
		public BlobNotFoundException(IoHash Hash)
			: base("Blob {Hash} was not found in the object store")
		{
			this.Hash = Hash;
		}
	}

	/// <summary>
	/// Extension methods for <see cref="BlobStore.BlobStoreClient"/>
	/// </summary>
	public static class BlobStoreExtensions
	{
		/// <summary>
		/// Gets a blob of data from the store
		/// </summary>
		/// <param name="BlobStore">The blob store instance</param>
		/// <param name="NamespaceId">Namespace for the data</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns>Byte array of blob data</returns>
		public static async Task<byte[]> GetBlobAsync(this BlobStore.BlobStoreClient BlobStore, string NamespaceId, IoHash Hash)
		{
			GetBlobsRequest Request = new GetBlobsRequest();
			Request.NamespaceId = NamespaceId;
			Request.Blobs.Add(Hash);

			GetBlobsResponse Response = await BlobStore.GetAsync(Request);
			foreach (GetBlobResponse BlobResponse in Response.Blobs)
			{
				if (BlobResponse.Exists)
				{
					return BlobResponse.Data.ToByteArray();
				}
			}

			throw new BlobNotFoundException(Hash);
		}

		/// <summary>
		/// Copies a blob of data from the store to a file on disk
		/// </summary>
		/// <param name="BlobStore">The blob store instance</param>
		/// <param name="NamespaceId">Namespace for the data</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="File">File to write. Any parent directories will be created if they do not already exist.</param>
		/// <returns>Byte array of blob data</returns>
		public static async Task GetFileAsync(this BlobStore.BlobStoreClient BlobStore, string NamespaceId, IoHash Hash, FileReference File)
		{
			byte[] Data = await GetBlobAsync(BlobStore, NamespaceId, Hash);
			DirectoryReference.CreateDirectory(File.Directory);
			await FileReference.WriteAllBytesAsync(File, Data);
		}

		/// <summary>
		/// Reads a compact binary object from the store
		/// </summary>
		/// <typeparam name="T">The type of object to return</typeparam>
		/// <param name="BlobStore">The blob store instance</param>
		/// <param name="NamespaceId">Namespace for the data</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns>Byte array of blob data</returns>
		public static async Task<T> GetObjectAsync<T>(this BlobStore.BlobStoreClient BlobStore, string NamespaceId, CbObjectAttachment Hash) where T : class
		{
			byte[] Data = await GetBlobAsync(BlobStore, NamespaceId, Hash);
			return CbSerializer.Deserialize<T>(new CbField(Data));
		}

		/// <summary>
		/// Writes a blob of data to the store
		/// </summary>
		/// <typeparam name="T">The type of object to return</typeparam>
		/// <param name="BlobStore">The blob store instance</param>
		/// <param name="NamespaceId">Namespace for the data</param>
		/// <param name="Data">The data to write</param>
		/// <returns>Byte array of blob data</returns>
		public static async Task<IoHash> PutBlobAsync(this BlobStore.BlobStoreClient BlobStore, string NamespaceId, ReadOnlyMemory<byte> Data)
		{
			IoHash Hash = IoHash.Compute(Data.Span);

			AddBlobsRequest Request = new AddBlobsRequest();
			Request.NamespaceId = NamespaceId;

			AddBlobRequest BlobRequest = new AddBlobRequest();
			BlobRequest.Hash = Hash;
			BlobRequest.Data = ByteString.CopyFrom(Data.Span);

			Request.Blobs.Add(BlobRequest);
			await BlobStore.AddAsync(Request);

			return Hash;
		}

		/// <summary>
		/// Writes a file to the store
		/// </summary>
		/// <typeparam name="T">The type of object to return</typeparam>
		/// <param name="BlobStore">The blob store instance</param>
		/// <param name="NamespaceId">Namespace for the data</param>
		/// <param name="File">Location of the file to write</param>
		/// <returns>Byte array of blob data</returns>
		public static async Task<IoHash> PutFileAsync(this BlobStore.BlobStoreClient BlobStore, string NamespaceId, FileReference File)
		{
			byte[] Data = await FileReference.ReadAllBytesAsync(File);
			return await PutBlobAsync(BlobStore, NamespaceId, Data);
		}

		/// <summary>
		/// Writes a compact binary object to the store
		/// </summary>
		/// <typeparam name="T">The type of object to return</typeparam>
		/// <param name="BlobStore">The blob store instance</param>
		/// <param name="NamespaceId">Namespace for the data</param>
		/// <param name="Item">The item to write</param>
		/// <returns>Byte array of blob data</returns>
		public static async Task<CbObjectAttachment> PutObjectAsync<T>(this BlobStore.BlobStoreClient BlobStore, string NamespaceId, T Item)
		{
			ReadOnlyMemory<byte> Data = CbSerializer.Serialize(Item).GetView();
			return await PutBlobAsync(BlobStore, NamespaceId, Data);
		}
	}
}
