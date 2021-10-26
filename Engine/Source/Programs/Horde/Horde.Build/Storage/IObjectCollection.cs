// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
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
	/// Interface for a collection of objects
	/// </summary>
	public interface IObjectCollection
	{
		/// <summary>
		/// Adds an item to storage
		/// </summary>
		/// <param name="NsId">The namespace id</param>
		/// <param name="Hash">Hash of the object</param>
		/// <param name="Data">The object data</param>
		Task AddAsync(NamespaceId NsId, IoHash Hash, CbObject Data);

		/// <summary>
		/// Reads an object from storage
		/// </summary>
		/// <param name="NsId">The namespace id</param>
		/// <param name="Hash">Hash of the object</param>
		/// <returns>Stream for the blob, or null if it does not exist</returns>
		Task<CbObject?> GetAsync(NamespaceId NsId, IoHash Hash);

		/// <summary>
		/// Determines which of a set of blobs exist
		/// </summary>
		/// <param name="NsId">The namespace id</param>
		/// <param name="Hashes"></param>
		/// <returns></returns>
		Task<List<IoHash>> ExistsAsync(NamespaceId NsId, IEnumerable<IoHash> Hashes);
	}

	/// <summary>
	/// Exeception thrown when an object is not found
	/// </summary>
	public class CbObjectNotFoundException : Exception
	{
		/// <summary>
		/// Namespace of the missing object
		/// </summary>
		public NamespaceId NsId { get; }

		/// <summary>
		/// Hash of the missing object
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NsId"></param>
		/// <param name="Hash"></param>
		public CbObjectNotFoundException(NamespaceId NsId, IoHash Hash)
			: base($"Missing object {Hash} in {NsId}")
		{
			this.NsId = NsId;
			this.Hash = Hash;
		}
	}

	/// <summary>
	/// Extension methods for <see cref="IObjectCollection"/>
	/// </summary>
	public static class ObjectCollectionExtensions
	{
		/// <summary>
		/// Adds an object
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="ObjectCollection"></param>
		/// <param name="NsId"></param>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static async Task<IoHash> AddAsync<T>(this IObjectCollection ObjectCollection, NamespaceId NsId, T Value) where T : class
		{
			CbObject ObjectValue = CbSerializer.Serialize<T>(Value);
			IoHash Hash = ObjectValue.GetHash();
			await ObjectCollection.AddAsync(NsId, Hash, ObjectValue);
			return Hash;
		}

		/// <summary>
		/// Gets a typed object from the store
		/// </summary>
		/// <param name="ObjectCollection"></param>
		/// <param name="NsId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<CbObject> GetRequiredObjectAsync(this IObjectCollection ObjectCollection, NamespaceId NsId, IoHash Hash)
		{
			CbObject? Object = await ObjectCollection.GetAsync(NsId, Hash);
			if (Object == null)
			{
				throw new CbObjectNotFoundException(NsId, Hash);
			}
			return Object;
		}

		/// <summary>
		/// Gets a typed object from the store
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="ObjectCollection"></param>
		/// <param name="NsId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<T?> GetAsync<T>(this IObjectCollection ObjectCollection, NamespaceId NsId, IoHash Hash) where T : class
		{
			CbObject? Object = await ObjectCollection.GetAsync(NsId, Hash);
			if (Object == null)
			{
				return null;
			}
			return CbSerializer.Deserialize<T>(Object.AsField());
		}

		/// <summary>
		/// Gets a typed object from the store
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="ObjectCollection"></param>
		/// <param name="NsId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<T> GetRequiredObjectAsync<T>(this IObjectCollection ObjectCollection, NamespaceId NsId, IoHash Hash) where T : class
		{
			CbObject? Object = await ObjectCollection.GetAsync(NsId, Hash);
			if (Object == null)
			{
				throw new CbObjectNotFoundException(NsId, Hash);
			}
			return CbSerializer.Deserialize<T>(Object.AsField());
		}

		/// <summary>
		/// Test whether a single blob exists
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<bool> ExistsAsync(this IObjectCollection Collection, NamespaceId NamespaceId, IoHash Hash)
		{
			List<IoHash> Hashes = await Collection.ExistsAsync(NamespaceId, new[] { Hash });
			return Hashes.Count > 0;
		}
	}
}
