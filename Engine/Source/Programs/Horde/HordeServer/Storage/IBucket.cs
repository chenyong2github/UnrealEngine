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
	/// Interface for a ref bucket
	/// </summary>
	public interface IBucket
	{
		/// <summary>
		/// Namespace identifier
		/// </summary>
		public NamespaceId NamespaceId { get; }

		/// <summary>
		/// Bucket identifier
		/// </summary>
		public BucketId BucketId { get; }

		/// <summary>
		/// Whether the bucket has been deleted
		/// </summary>
		public bool Deleted { get; }
	}
}
