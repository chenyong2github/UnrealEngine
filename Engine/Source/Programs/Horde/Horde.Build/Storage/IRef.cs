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
	/// Interface for a collection of template documents
	/// </summary>
	public interface IRef
	{
		/// <summary>
		/// Namespace identifier
		/// </summary>
		NamespaceId NamespaceId { get; }

		/// <summary>
		/// Bucket identifier
		/// </summary>
		BucketId BucketId { get; }

		/// <summary>
		/// Ref name
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Last access time
		/// </summary>
		DateTime LastAccessTime { get; }

		/// <summary>
		/// Value for the ref
		/// </summary>
		CbObject Value { get; }

		/// <summary>
		/// Whether the ref has been finalized
		/// </summary>
		bool Finalized { get; }
	}
}
