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
		Task<Stream?> TryReadStreamAsync(NamespaceId NamespaceId, IoHash Hash);

		/// <summary>
		/// Reads a blob as an array of bytes. Throws an exception if the blob is too large.
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns>Stream for the blob, or null if it does not exist</returns>
		Task<ReadOnlyMemory<byte>?> TryReadBytesAsync(NamespaceId NamespaceId, IoHash Hash);

		/// <summary>
		/// Adds an item to storage
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="Stream">The stream to write</param>
		Task WriteStreamAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream);

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash">Expected hash of the data</param>
		/// <param name="Data">The data to be written</param>
		/// <returns></returns>
		Task WriteBytesAsync(NamespaceId NamespaceId, IoHash Hash, ReadOnlyMemory<byte> Data);

		/// <summary>
		/// Determines which of a set of blobs exist
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hashes"></param>
		/// <returns></returns>
		Task<List<IoHash>> ExistsAsync(NamespaceId NamespaceId, IEnumerable<IoHash> Hashes);
	}

	/// <summary>
	/// Exception thrown for missing blobs
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
		/// <param name="Hash">Hash of the missing blob</param>
		public BlobNotFoundException(IoHash Hash) : base($"Unable to find blob {Hash}")
		{
			this.Hash = Hash;
		}
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
		public static async Task<ReadOnlyMemory<byte>> ReadBytesAsync(this IBlobCollection Collection, NamespaceId NamespaceId, IoHash Hash)
		{
			ReadOnlyMemory<byte>? Result = await Collection.TryReadBytesAsync(NamespaceId, Hash);
			if (Result == null)
			{
				throw new BlobNotFoundException(Hash);
			}
			return Result.Value;
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
			await Collection.WriteBytesAsync(NamespaceId, Hash, Data);
			return Hash;
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
