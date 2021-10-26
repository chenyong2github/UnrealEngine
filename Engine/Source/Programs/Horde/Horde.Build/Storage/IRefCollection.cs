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
	using BucketId = StringId<IBucket>;

	/// <summary>
	/// Interface for a collection of ref documents
	/// </summary>
	public interface IRefCollection
	{
		/// <summary>
		/// Sets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="Name">Name of the reference</param>
		/// <param name="Value">New value for the reference, as a compact binary object</param>
		/// <returns>List of missing references</returns>
		Task<List<IoHash>> SetAsync(NamespaceId NamespaceId, BucketId BucketId, string Name, CbObject Value);

		/// <summary>
		/// Attempts to finalize a reference, turning its references into hard references
		/// </summary>
		/// <param name="Ref">The ref to finalize</param>
		/// <returns></returns>
		Task<List<IoHash>> FinalizeAsync(IRef Ref);

		/// <summary>
		/// Gets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="Name">Name of the reference</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<IRef?> GetAsync(NamespaceId NamespaceId, BucketId BucketId, string Name);

		/// <summary>
		/// Touches the given reference, updating it's last access time
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="Name">Name of the reference</param>
		/// <returns>True if the ref exists</returns>
		Task<bool> TouchAsync(NamespaceId NamespaceId, BucketId BucketId, string Name);

		/// <summary>
		/// Removes the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefName">Name of the reference</param>
		/// <returns>True if the ref was deleted, false if it did not exist</returns>
		Task<bool> DeleteAsync(NamespaceId NamespaceId, BucketId BucketId, string RefName);
	}
}
