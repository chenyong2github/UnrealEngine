// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel;
using EpicGames.Core;
using Horde.Build.Utilities;
using MongoDB.Bson;

namespace Horde.Build.Agents.Leases
{
	/// <summary>
	/// Identifier for a user
	/// </summary>
	/// <param name="Id">Id to construct from</param>
	[JsonSchemaString]
	[TypeConverter(typeof(ObjectIdTypeConverter<LeaseId, LeaseIdConverter>))]
	[ObjectIdConverter(typeof(LeaseIdConverter))]
	public record struct LeaseId(ObjectId Id)
	{
		/// <summary>
		/// Creates a new <see cref="LeaseId"/>
		/// </summary>
		public static LeaseId GenerateNewId() => new LeaseId(ObjectId.GenerateNewId());

		/// <inheritdoc cref="ObjectId.Parse(System.String)"/>
		public static LeaseId Parse(string text) => new LeaseId(ObjectId.Parse(text));

		/// <inheritdoc/>
		public override string ToString() => Id.ToString();
	}

	/// <summary>
	/// Converter to and from <see cref="ObjectId"/> instances.
	/// </summary>
	class LeaseIdConverter : ObjectIdConverter<LeaseId>
	{
		/// <inheritdoc/>
		public override LeaseId FromObjectId(ObjectId id) => new LeaseId(id);

		/// <inheritdoc/>
		public override ObjectId ToObjectId(LeaseId value) => value.Id;
	}
}
