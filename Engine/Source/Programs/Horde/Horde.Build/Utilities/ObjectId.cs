// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization.Converters;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Serializers;
using System;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Normalized string identifier for a resource
	/// </summary>
	[JsonSchemaString]
	[TypeConverter(typeof(ObjectIdTypeConverter))]
	public struct ObjectId<T> : IEquatable<ObjectId<T>>, IComparable<ObjectId<T>>
	{
		/// <summary>
		/// Empty string
		/// </summary>
		[SuppressMessage("Design", "CA1000:Do not declare static members on generic types", Justification = "<Pending>")]
		public static ObjectId<T> Empty { get; } = new ObjectId<T>(ObjectId.Empty);

		/// <summary>
		/// The text representing this id
		/// </summary>
		public ObjectId Value { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public ObjectId(ObjectId Value) => this.Value = Value;

		/// <summary>
		/// Constructor
		/// </summary>
		public ObjectId(byte[] Bytes) => this.Value = new ObjectId(Bytes);

		/// <summary>
		/// Constructor
		/// </summary>
		public ObjectId(string Value) => this.Value = new ObjectId(Value);

		/// <inheritdoc cref="ObjectId.GenerateNewId()"/>
		[SuppressMessage("Design", "CA1000:Do not declare static members on generic types", Justification = "<Pending>")]
		public static ObjectId<T> GenerateNewId() => new ObjectId<T>(ObjectId.GenerateNewId());

		/// <inheritdoc cref="ObjectId.Parse(string)"/>
		[SuppressMessage("Design", "CA1000:Do not declare static members on generic types", Justification = "<Pending>")]
		public static ObjectId<T> Parse(string Str) => new ObjectId<T>(ObjectId.Parse(Str));

		/// <inheritdoc cref="ObjectId.TryParse(string, out ObjectId)"/>
		[SuppressMessage("Design", "CA1000:Do not declare static members on generic types", Justification = "<Pending>")]
		public static bool TryParse(string Str, out ObjectId<T> Value)
		{
			if (ObjectId.TryParse(Str, out ObjectId Id))
			{
				Value = new ObjectId<T>(Id);
				return true;
			}
			else
			{
				Value = Empty;
				return false;
			}
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is ObjectId<T> Other && Equals(Other);

		/// <inheritdoc/>
		public override int GetHashCode() => Value.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(ObjectId<T> Other) => Value.Equals(Other.Value);

		/// <inheritdoc/>
		public override string ToString() => Value.ToString();

		/// <inheritdoc/>
		public int CompareTo(ObjectId<T> Other) => Value.CompareTo(Other.Value);

		/// <inheritdoc cref="ObjectId.op_LessThan"/>
		public static bool operator <(ObjectId<T> Left, ObjectId<T> Right) => Left.Value < Right.Value;

		/// <inheritdoc cref="ObjectId.op_GreaterThan"/>
		public static bool operator >(ObjectId<T> Left, ObjectId<T> Right) => Left.Value > Right.Value;

		/// <inheritdoc cref="ObjectId.op_LessThanOrEqual"/>
		public static bool operator <=(ObjectId<T> Left, ObjectId<T> Right) => Left.Value <= Right.Value;

		/// <inheritdoc cref="ObjectId.op_GreaterThanOrEqual"/>
		public static bool operator >=(ObjectId<T> Left, ObjectId<T> Right) => Left.Value >= Right.Value;

		/// <inheritdoc cref="ObjectId.op_Equality"/>
		public static bool operator ==(ObjectId<T> Left, ObjectId<T> Right) => Left.Value == Right.Value;

		/// <inheritdoc cref="ObjectId.op_Inequality"/>
		public static bool operator !=(ObjectId<T> Left, ObjectId<T> Right) => Left.Value != Right.Value;
	}

	/// <summary>
	/// Converts <see cref="ObjectId{T}"/> values to and from JSON
	/// </summary>
	public class ObjectIdJsonConverter<T> : JsonConverter<ObjectId<T>>
	{
		/// <inheritdoc/>
		public override ObjectId<T> Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
		{
			return new ObjectId<T>(ObjectId.Parse(Reader.GetString()!));
		}

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, ObjectId<T> Value, JsonSerializerOptions Options)
		{
			Writer.WriteStringValue(Value.ToString());
		}
	}

	/// <summary>
	/// Converts <see cref="ObjectId{T}"/> values to and from JSON
	/// </summary>
	public class JsonObjectIdConverterFactory : JsonConverterFactory
	{
		/// <inheritdoc/>
		public override bool CanConvert(Type TypeToConvert)
		{
			return TypeToConvert.IsGenericType && TypeToConvert.GetGenericTypeDefinition() == typeof(ObjectId<>);
		}

		/// <inheritdoc/>
		public override JsonConverter? CreateConverter(Type Type, JsonSerializerOptions Options)
		{
			return (JsonConverter?)Activator.CreateInstance(typeof(ObjectIdJsonConverter<>).MakeGenericType(Type.GetGenericArguments()));
		}
	}

	/// <summary>
	/// Serializer for ObjectId objects
	/// </summary>
	public sealed class ObjectIdBsonSerializer<T> : SerializerBase<ObjectId<T>>
	{
		/// <inheritdoc/>
		public override ObjectId<T> Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			return new ObjectId<T>(Context.Reader.ReadObjectId());
		}

		/// <inheritdoc/>
		public override void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, ObjectId<T> Value)
		{
			Context.Writer.WriteObjectId(Value.Value);
		}
	}

	/// <summary>
	/// Serializer for ObjectId objects
	/// </summary>
	public sealed class ObjectIdSerializationProvider : BsonSerializationProviderBase
	{
		/// <inheritdoc/>
		public override IBsonSerializer? GetSerializer(Type Type, IBsonSerializerRegistry SerializerRegistry)
		{
			if (Type.IsGenericType && Type.GetGenericTypeDefinition() == typeof(ObjectId<>))
			{
				return (IBsonSerializer?)Activator.CreateInstance(typeof(ObjectIdBsonSerializer<>).MakeGenericType(Type.GetGenericArguments()));
			}
			else
			{
				return null;
			}
		}
	}

	/// <summary>
	/// Type converter from strings to PropertyFilter objects
	/// </summary>
	sealed class ObjectIdTypeConverter : TypeConverter
	{
		Type Type;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type"></param>
		public ObjectIdTypeConverter(Type Type)
		{
			this.Type = Type;
		}

		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext? Context, Type SourceType)
		{
			return SourceType == typeof(string) || base.CanConvertFrom(Context, SourceType);
		}

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext? Context, CultureInfo? Culture, object Value)
		{
			return Activator.CreateInstance(Type, Value)!;
		}

		/// <inheritdoc/>
		public override bool CanConvertTo(ITypeDescriptorContext? Context, Type? DestinationType)
		{
			if (DestinationType == null)
			{
				return false;
			}
			if (DestinationType == typeof(string))
			{
				return true;
			}
			if (DestinationType.IsGenericType)
			{
				Type GenericTypeDefinition = DestinationType.GetGenericTypeDefinition();
				if (GenericTypeDefinition == typeof(ObjectId<>))
				{
					return true;
				}
				if (GenericTypeDefinition == typeof(Nullable<>))
				{
					return CanConvertTo(Context, GenericTypeDefinition.GetGenericArguments()[0]);
				}
			}
			return false;
		}

		/// <inheritdoc/>
		public override object? ConvertTo(ITypeDescriptorContext? Context, CultureInfo? Culture, object? Value, Type DestinationType)
		{
			if (DestinationType == typeof(string))
			{
				return Value?.ToString();
			}
			else
			{
				return Activator.CreateInstance(DestinationType, Value);
			}
		}
	}
}
