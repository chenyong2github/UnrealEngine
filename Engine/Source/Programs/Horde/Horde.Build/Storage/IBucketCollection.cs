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
	using BucketId = StringId<IBucket>;

	/// <summary>
	/// Interface for a collection of template documents
	/// </summary>
	public interface IBucketCollection
	{
		/// <summary>
		/// Adds or updates a bucket configuration
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="Bucket">New settings for the bucket</param>
		Task AddOrUpdateAsync(NamespaceId NamespaceId, BucketId BucketId, BucketConfig Bucket);

		/// <summary>
		/// Attempts to get a bucket by identifier
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <returns>Bucket definition</returns>
		Task<IBucket?> GetAsync(NamespaceId NamespaceId, BucketId BucketId);

		/// <summary>
		/// Remove a bucket definition
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <returns>Bucket definition</returns>
		Task RemoveAsync(NamespaceId NamespaceId, BucketId BucketId);

		/// <summary>
		/// Find all buckets within a namespace
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <returns>List of buckets within the namespace</returns>
		Task<List<IBucket>> FindAsync(NamespaceId NamespaceId);
	}
}
