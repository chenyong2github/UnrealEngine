// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Serializers;
using Newtonsoft.Json.Bson;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Serializer for DateTimeOffset structs, which writes data as a string
	/// </summary>
	class DateTimeOffsetStringSerializer : StructSerializerBase<DateTimeOffset>
	{
		/// <inheritdoc/>
		public override DateTimeOffset Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			switch (Context.Reader.CurrentBsonType)
			{
				case BsonType.String:
					string StringValue = Context.Reader.ReadString();
					return DateTimeOffset.Parse(StringValue, DateTimeFormatInfo.InvariantInfo);
				case BsonType.DateTime:
					long Ticks = Context.Reader.ReadDateTime();
					return DateTimeOffset.FromUnixTimeMilliseconds(Ticks);
				default:
					throw new FormatException($"Unable to deserialize a DateTimeOffset from a {Context.Reader.CurrentBsonType}");
			}
		}

		/// <inheritdoc/>
		public override void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, DateTimeOffset Value)
		{
			Context.Writer.WriteString(Value.ToString(DateTimeFormatInfo.InvariantInfo));
		}
	}
}
