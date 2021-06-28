// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Storage
{
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Interface for a collection of blobs
	/// </summary>
	public interface IBlobCollection
	{
		/// <summary>
		/// Opens a blob read stream
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns>Stream for the blob, or null if it does not exist</returns>
		Task<Stream?> ReadAsync(NamespaceId NamespaceId, IoHash Hash);

		/// <summary>
		/// Adds an item to storage
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="Stream">The stream to write</param>
		Task WriteAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream);

		/// <summary>
		/// Determines which of a set of blobs exist
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hashes"></param>
		/// <returns></returns>
		Task<List<IoHash>> ExistsAsync(NamespaceId NamespaceId, IEnumerable<IoHash> Hashes);
	}

	/// <summary>
	/// Extension methods for <see cref="IBlobCollection"/>
	/// </summary>
	public static class BlobCollectionExtensions
	{
		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<byte[]?> ReadBytesAsync(this IBlobCollection Collection, NamespaceId NamespaceId, IoHash Hash)
		{
			using (MemoryStream OutputStream = new MemoryStream())
			{
				using (Stream? InputStream = await Collection.ReadAsync(NamespaceId, Hash))
				{
					if(InputStream == null)
					{
						return null;
					}

					await InputStream.CopyToAsync(OutputStream);
					return OutputStream.ToArray();
				}
			}
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Data">The data to be written</param>
		/// <returns></returns>
		public static async Task<IoHash> WriteBytesAsync(this IBlobCollection Collection, NamespaceId NamespaceId, ReadOnlyMemory<byte> Data)
		{
			IoHash Hash = IoHash.Compute(Data.Span);
			await WriteBytesAsync(Collection, NamespaceId, Hash, Data);
			return Hash;
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash">Expected hash of the data</param>
		/// <param name="Data">The data to be written</param>
		/// <returns></returns>
		public static async Task WriteBytesAsync(this IBlobCollection Collection, NamespaceId NamespaceId, IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			using (ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Data))
			{
				await Collection.WriteAsync(NamespaceId, Hash, Stream);
			}
		}

		/// <summary>
		/// Test whether a single blob exists
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<bool> ExistsAsync(this IBlobCollection Collection, NamespaceId NamespaceId, IoHash Hash)
		{
			List<IoHash> Hashes = await Collection.ExistsAsync(NamespaceId, new[] { Hash });
			return Hashes.Count > 0;
		}
	}
}
