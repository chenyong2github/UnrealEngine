// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using EpicGames.Serialization.Converters;
using HordeServer.Models;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Bson.Serialization.Serializers;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Normalized string identifier for a resource
	/// </summary>
	[JsonSchemaString]
	[TypeConverter(typeof(StringIdTypeConverter))]
	[CbConverter(typeof(CbStringIdConverter<>))]
	public struct StringId<T> : IEquatable<StringId<T>>
	{
		/// <summary>
		/// Empty string
		/// </summary>
		[SuppressMessage("Design", "CA1000:Do not declare static members on generic types", Justification = "<Pending>")]
		public static StringId<T> Empty { get; } = new StringId<T>(String.Empty);

		/// <summary>
		/// The text representing this id
		/// </summary>
		readonly string Text;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Input">Unique id for the string</param>
		[SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase", Justification = "<Pending>")]
		public StringId(string Input)
		{
			this.Text = Input;

			if (Text.Length == 0)
			{
//				throw new ArgumentException("String id may not be empty");
			}

			const int MaxLength = 64;
			if (Text.Length > MaxLength)
			{
				throw new ArgumentException($"String id may not be longer than {MaxLength} characters");
			}

			for (int Idx = 0; Idx < Text.Length; Idx++)
			{
				char Character = Text[Idx];
				if (!IsValidCharacter(Character))
				{
					if (Character >= 'A' && Character <= 'Z')
					{
						Text = Text.ToLowerInvariant();
					}
					else
					{
						throw new ArgumentException($"{Text} is not a valid string id");
					}
				}
			}
		}

		/// <summary>
		/// Constructs from a nullable string
		/// </summary>
		/// <param name="Text">The text to construct from</param>
		/// <returns></returns>
		[SuppressMessage("Design", "CA1000:Do not declare static members on generic types")]
		public static StringId<T>? FromNullable(string? Text)
		{
			if (String.IsNullOrEmpty(Text))
			{
				return null;
			}
			else
			{
				return new StringId<T>(Text);
			}
		}

		/// <summary>
		/// Generates a new string id from the given text
		/// </summary>
		/// <param name="Text">Text to generate from</param>
		/// <returns>New string id</returns>
		[SuppressMessage("Design", "CA1000:Do not declare static members on generic types", Justification = "<Pending>")]
		public static StringId<T> Sanitize(string Text)
		{
			StringBuilder Result = new StringBuilder();
			for (int Idx = 0; Idx < Text.Length; Idx++)
			{
				char Character = Text[Idx];
				if (Character >= 'A' && Character <= 'Z')
				{
					Result.Append((char)('a' + (Character - 'A')));
				}
				else if (IsValidCharacter(Character))
				{
					Result.Append(Character);
				}
				else if (Result.Length > 0 && Result[Result.Length - 1] != '-')
				{
					Result.Append('-');
				}
			}
			while(Result.Length > 0 && Result[Result.Length - 1] == '-')
			{
				Result.Remove(Result.Length - 1, 1);
			}
			return new StringId<T>(Result.ToString());
		}

		/// <summary>
		/// Checks whether this StringId is set
		/// </summary>
		public bool IsEmpty
		{
			get { return String.IsNullOrEmpty(Text); }
		}

		/// <summary>
		/// Checks whether the given character is valid within a string id
		/// </summary>
		/// <param name="Character">The character to check</param>
		/// <returns>True if the character is valid</returns>
		static bool IsValidCharacter(char Character)
		{
			if (Character >= 'a' && Character <= 'z')
			{
				return true;
			}
			if (Character >= '0' && Character <= '9')
			{
				return true;
			}
			if (Character == '-' || Character == '_' || Character == '.')
			{
				return true;
			}
			return false;
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj)
		{
			return Obj is StringId<T> && Equals((StringId<T>)Obj);
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			return Text.GetHashCode(StringComparison.Ordinal);
		}

		/// <inheritdoc/>
		public bool Equals(StringId<T> Other)
		{
			return Text.Equals(Other.Text, StringComparison.Ordinal);
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return Text;
		}

		/// <summary>
		/// Compares two string ids for equality
		/// </summary>
		/// <param name="Left">The first string id</param>
		/// <param name="Right">Second string id</param>
		/// <returns>True if the two string ids are equal</returns>
		public static bool operator ==(StringId<T> Left, StringId<T> Right)
		{
			return Left.Equals(Right);
		}

		/// <summary>
		/// Compares two string ids for inequality
		/// </summary>
		/// <param name="Left">The first string id</param>
		/// <param name="Right">Second string id</param>
		/// <returns>True if the two string ids are not equal</returns>
		public static bool operator !=(StringId<T> Left, StringId<T> Right)
		{
			return !Left.Equals(Right);
		}
	}

	/// <summary>
	/// Converts <see cref="StringId{T}"/> values to and from JSON
	/// </summary>
	public class StringIdJsonConverter<T> : JsonConverter<StringId<T>>
	{
		/// <inheritdoc/>
		public override StringId<T> Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
		{
			return new StringId<T>(Reader.GetString()!);
		}

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, StringId<T> Value, JsonSerializerOptions Options)
		{
			Writer.WriteStringValue(Value.ToString());
		}
	}

	/// <summary>
	/// Converts <see cref="StringId{T}"/> values to and from JSON
	/// </summary>
	public class JsonStringIdConverterFactory : JsonConverterFactory
	{
		/// <inheritdoc/>
		public override bool CanConvert(Type TypeToConvert)
		{
			return TypeToConvert.IsGenericType && TypeToConvert.GetGenericTypeDefinition() == typeof(StringId<>);
		}

		/// <inheritdoc/>
		public override JsonConverter? CreateConverter(Type Type, JsonSerializerOptions Options)
		{
			return (JsonConverter?)Activator.CreateInstance(typeof(StringIdJsonConverter<>).MakeGenericType(Type.GetGenericArguments()));
		}
	}

	/// <summary>
	/// Serializer for StringId objects
	/// </summary>
	public sealed class StringIdBsonSerializer<T> : SerializerBase<StringId<T>>
	{
		/// <inheritdoc/>
		public override StringId<T> Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			string Argument;
			if (Context.Reader.CurrentBsonType == MongoDB.Bson.BsonType.ObjectId)
			{
				Argument = Context.Reader.ReadObjectId().ToString();
			}
			else
			{
				Argument = Context.Reader.ReadString();
			}
			return new StringId<T>(Argument);
		}

		/// <inheritdoc/>
		public override void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, StringId<T> Value)
		{
			Context.Writer.WriteString(Value.ToString());
		}
	}

	/// <summary>
	/// Serializer for StringId objects
	/// </summary>
	public sealed class StringIdSerializationProvider : BsonSerializationProviderBase
	{
		/// <inheritdoc/>
		public override IBsonSerializer? GetSerializer(Type type, IBsonSerializerRegistry serializerRegistry)
		{
			if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(StringId<>))
			{
				return (IBsonSerializer?)Activator.CreateInstance(typeof(StringIdBsonSerializer<>).MakeGenericType(type.GetGenericArguments()));
			}
			else
			{
				return null;
			}
		}
	}

	sealed class CbStringIdConverter<T> : CbConverter<StringId<T>>
	{
		public override StringId<T> Read(CbField Field)
		{
			return new StringId<T>(Field.AsString().ToString());
		}

		public override void Write(CbWriter Writer, StringId<T> Value)
		{
			Writer.WriteStringValue(Value.ToString());
		}

		public override void WriteNamed(CbWriter Writer, Utf8String Name, StringId<T> Value)
		{
			Writer.WriteString(Name, Value.ToString());
		}
	}

	/// <summary>
	/// Type converter from strings to PropertyFilter objects
	/// </summary>
	sealed class StringIdTypeConverter : TypeConverter
	{
		Type Type;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type"></param>
		public StringIdTypeConverter(Type Type)
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
				if (GenericTypeDefinition == typeof(StringId<>))
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
