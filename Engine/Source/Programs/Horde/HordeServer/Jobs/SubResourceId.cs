// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Identifier for subresources. Assigning unique ids to subresources prevents against race conditions using indices when subresources are added and removed.
	/// 
	/// Subresource identifiers are stored as 16-bit integers formatted as a 4-digit hex code, in order to keep URLs short. Calling Next() will generate a new
	/// identifier with more entropy than just incrementing the value but an identical period before repeating, in order to make URL fragments more distinctive.
	/// </summary>
	[BsonSerializer(typeof(SubResourceIdSerializer))]
	public struct SubResourceId : IEquatable<SubResourceId>
	{
		/// <summary>
		/// The unique identifier value
		/// </summary>
		public ushort Value { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Value">New identifier for this subresource</param>
		public SubResourceId(ushort Value)
		{
			this.Value = Value;
		}

		/// <summary>
		/// Creates a new random subresource id. We use random numbers for this to increase distinctiveness.
		/// </summary>
		/// <returns>New subresource id</returns>
		public static SubResourceId Random()
		{
			return new SubResourceId((ushort)Stopwatch.GetTimestamp());
		}

		/// <summary>
		/// Updates the current value, and returns a copy of the previous value.
		/// </summary>
		/// <returns>New subresource identifier</returns>
		public SubResourceId Next()
		{
			// 771 is a factor of 65535, so we won't repeat when wrapping round 64k.
			return new SubResourceId((ushort)((int)Value + 771));
		}

		/// <summary>
		/// Parse a subresource id from a string
		/// </summary>
		/// <param name="Text">Text to parse</param>
		/// <returns>New subresource id</returns>
		public static SubResourceId Parse(string Text)
		{
			return new SubResourceId(ushort.Parse(Text, NumberStyles.HexNumber, CultureInfo.InvariantCulture));
		}

		/// <summary>
		/// Converts this identifier to a string
		/// </summary>
		/// <returns>String representation of this id</returns>
		public override string ToString()
		{
			return Value.ToString("x4", CultureInfo.InvariantCulture);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Other)
		{
			SubResourceId? OtherId = Other as SubResourceId?;
			return OtherId != null && Equals(OtherId.Value);
		}

		/// <inheritdoc/>
		public bool Equals(SubResourceId Other)
		{
			return Value == Other.Value;
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			return (int)Value;
		}

		/// <summary>
		/// Equality operator for identifiers
		/// </summary>
		/// <param name="Left">First identifier to compare</param>
		/// <param name="Right">Second identifier to compare</param>
		/// <returns>True if the identifiers are equal</returns>
		public static bool operator ==(SubResourceId Left, SubResourceId Right)
		{
			return Left.Value == Right.Value;
		}

		/// <summary>
		/// Inequality operator for identifiers
		/// </summary>
		/// <param name="Left">First identifier to compare</param>
		/// <param name="Right">Second identifier to compare</param>
		/// <returns>True if the identifiers are equal</returns>
		public static bool operator !=(SubResourceId Left, SubResourceId Right)
		{
			return Left.Value != Right.Value;
		}
	}

	/// <summary>
	/// Extension methods for manipulating subresource ids
	/// </summary>
	public static class SubResourceIdExtensions
	{
		/// <summary>
		/// Parse a string as a subresource identifier
		/// </summary>
		/// <param name="Text">Text to parse</param>
		/// <returns>The new subresource identifier</returns>
		public static SubResourceId ToSubResourceId(this string Text)
		{
			return new SubResourceId(ushort.Parse(Text, NumberStyles.HexNumber, CultureInfo.InvariantCulture));
		}
	}

	/// <summary>
	/// Serializer for subresource ids
	/// </summary>
	public class SubResourceIdSerializer : IBsonSerializer<SubResourceId>
	{
		/// <inheritdoc/>
		public Type ValueType => typeof(SubResourceId);

		/// <inheritdoc/>
		public object Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			return new SubResourceId((ushort)Context.Reader.ReadInt32());
		}

		/// <inheritdoc/>
		public void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, object Value)
		{
			SubResourceId Id = (SubResourceId)Value;
			Context.Writer.WriteInt32((int)Id.Value);
		}

		/// <inheritdoc/>
		public void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, SubResourceId Id)
		{
			Context.Writer.WriteInt32((int)Id.Value);
		}

		/// <inheritdoc/>
		SubResourceId IBsonSerializer<SubResourceId>.Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Vrgs)
		{
			return new SubResourceId((ushort)Context.Reader.ReadInt32());
		}
	}
}
