// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel;
using EpicGames.Core;
using Horde.Build.Utilities;
using MongoDB.Bson;

namespace Horde.Build.Users
{
	/// <summary>
	/// Identifier for a user
	/// </summary>
	/// <param name="Id">Id to construct from</param>
	[JsonSchemaString]
	[TypeConverter(typeof(ObjectIdTypeConverter<UserId, UserIdConverter>))]
	[ObjectIdConverter(typeof(UserIdConverter))]
	public record struct UserId(ObjectId Id)
	{
		/// <summary>
		/// Constant value for empty user id
		/// </summary>
		public static UserId Empty { get; } = new UserId(ObjectId.Empty);

		/// <summary>
		/// Creates a new <see cref="UserId"/>
		/// </summary>
		public static UserId GenerateNewId() => new UserId(ObjectId.GenerateNewId());

		/// <inheritdoc cref="ObjectId.Parse(System.String)"/>
		public static UserId Parse(string text) => new UserId(ObjectId.Parse(text));

		/// <inheritdoc cref="ObjectId.TryParse(System.String, out ObjectId)"/>
		public static bool TryParse(string text, out UserId id)
		{
			if (ObjectId.TryParse(text, out ObjectId objectId))
			{
				id = new UserId(objectId);
				return true;
			}
			else
			{
				id = default;
				return false;
			}
		}

		/// <inheritdoc/>
		public override string ToString() => Id.ToString();
	}

	/// <summary>
	/// Converter to and from <see cref="ObjectId"/> instances.
	/// </summary>
	class UserIdConverter : ObjectIdConverter<UserId>
	{
		/// <inheritdoc/>
		public override UserId FromObjectId(ObjectId id) => new UserId(id);

		/// <inheritdoc/>
		public override ObjectId ToObjectId(UserId value) => value.Id;
	}
}
