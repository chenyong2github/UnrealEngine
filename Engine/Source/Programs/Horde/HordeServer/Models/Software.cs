// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Information about agent software
	/// </summary>
	public interface ISoftware
	{
		/// <summary>
		/// Identifier for the app. Randomly generated.
		/// </summary>
		public ObjectId Id { get; }

		/// <summary>
		/// Zipped data for this app
		/// </summary>
		public byte[]? Data { get; }

		/// <summary>
		/// User that created this client
		/// </summary>
		public string? UploadedByUser { get; }

		/// <summary>
		/// Time at which the client was created
		/// </summary>
		public DateTime UploadedAtTime { get; }

		/// <summary>
		/// User that made this client the default
		/// </summary>
		public string? MadeDefaultByUser { get; }

		/// <summary>
		/// Timestamp at which this app was made the default
		/// </summary>
		public DateTime? MadeDefaultAtTime { get; }
	}
}
