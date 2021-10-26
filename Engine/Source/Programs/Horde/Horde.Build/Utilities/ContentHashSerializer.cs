// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using MongoDB.Bson.Serialization;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Serializer for JobStepRefId objects
	/// </summary>
	public sealed class ContentHashSerializer : IBsonSerializer<ContentHash>
	{
		/// <inheritdoc/>
		public Type ValueType
		{
			get { return typeof(ContentHash); }
		}

		/// <inheritdoc/>
		void IBsonSerializer.Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, object Value)
		{
			Serialize(Context, Args, (ContentHash)Value);
		}

		/// <inheritdoc/>
		object IBsonSerializer.Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			return ((IBsonSerializer<ContentHash>)this).Deserialize(Context, Args);
		}

		/// <inheritdoc/>
		public ContentHash Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			if (Context.Reader.CurrentBsonType == MongoDB.Bson.BsonType.ObjectId)
			{
				return new ContentHash(Context.Reader.ReadObjectId().ToByteArray());
			}
			else
			{
				return ContentHash.Parse(Context.Reader.ReadString());
			}
		}

		/// <inheritdoc/>
		public void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, ContentHash Value)
		{
			Context.Writer.WriteString(Value.ToString());
		}
	}
}
