// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
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
	/// Stores information about a credential
	/// </summary>
	public class Credential
	{
		/// <summary>
		/// Unique id for this credential
		/// </summary>
		[BsonId]
		public ObjectId Id { get; set; }

		/// <summary>
		/// Name of this credential
		/// </summary>
		[BsonRequired]
		public string Name { get; set; }

		/// <summary>
		/// The normalized name of this credential
		/// </summary>
		[BsonRequired]
		public string NormalizedName { get; set; }

		/// <summary>
		/// Properties for this credential
		/// </summary>
		public Dictionary<string, string> Properties { get; set; } = new Dictionary<string, string>();

		/// <summary>
		/// The ACL for this credential
		/// </summary>
		public Acl? Acl { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		[BsonConstructor]
		private Credential()
		{
			this.Name = null!;
			this.NormalizedName = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of this credential</param>
		public Credential(string Name)
		{
			this.Id = ObjectId.GenerateNewId();
			this.Name = Name;
			this.NormalizedName = GetNormalizedName(Name);
		}

		/// <summary>
		/// Gets the normalized form of a name, used for case-insensitive comparisons
		/// </summary>
		/// <param name="Name">The name to normalize</param>
		/// <returns>The normalized name</returns>
		public static string GetNormalizedName(string Name)
		{
			return Name.ToUpperInvariant();
		}
	}
}
