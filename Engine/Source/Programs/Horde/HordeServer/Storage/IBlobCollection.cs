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
		/// Adds an item to storage
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="Blob">The blob stream data</param>
		Task AddAsync(NamespaceId NamespaceId, IoHash Hash, Stream Blob);

		/// <summary>
		/// Opens a blob read stream
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns>Stream for the blob, or null if it does not exist</returns>
		Task<Stream?> GetAsync(NamespaceId NamespaceId, IoHash Hash);

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
		/// Adds a blob to the collection
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Blob"></param>
		/// <returns></returns>
		public static async Task<IoHash> AddAsync(this IBlobCollection Collection, NamespaceId NamespaceId, ReadOnlyMemory<byte> Blob)
		{
			IoHash Hash = IoHash.Compute(Blob.Span);
			await AddAsync(Collection, NamespaceId, Hash, Blob);
			return Hash;
		}

		/// <summary>
		/// Adds a blob to the collection
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <param name="Blob"></param>
		/// <returns></returns>
		public static Task AddAsync(this IBlobCollection Collection, NamespaceId NamespaceId, IoHash Hash, ReadOnlyMemory<byte> Blob)
		{
			using ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Blob);
			return Collection.AddAsync(NamespaceId, Hash, Stream);
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<byte[]?> GetByteArrayAsync(this IBlobCollection Collection, NamespaceId NamespaceId, IoHash Hash)
		{
			using Stream? Stream = await Collection.GetAsync(NamespaceId, Hash);
			if(Stream == null)
			{
				return null;
			}

			byte[] Data = new byte[Stream.Length];
			Stream.Read(Data);
			return Data;
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
