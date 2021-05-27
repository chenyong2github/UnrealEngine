// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Entry to the conform list
	/// </summary>
	public class ConformListEntry
	{
		/// <summary>
		/// The agent id
		/// </summary>
		[BsonElement("a")]
		public AgentId AgentId { get; set; }

		/// <summary>
		/// The lease id
		/// </summary>
		[BsonElement("l")]
		public ObjectId LeaseId { get; set; }

		/// <summary>
		/// Last timestamp that the lease was checked for validity 
		/// </summary>
		[BsonElement("t"), BsonIgnoreIfNull]
		public DateTime? LastCheckTimeUtc { get; set; }
	}

	/// <summary>
	/// List of machines that are currently conforming
	/// </summary>
	[SingletonDocument("60afc737f0d2a70754229300")]
	public class ConformList : SingletonBase
	{
		/// <summary>
		/// List of entries
		/// </summary>
		public List<ConformListEntry> Entries { get; set; } = new List<ConformListEntry>();
	}
}
