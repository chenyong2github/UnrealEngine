// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Sets a name used to discriminate between classes derived from a base class
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class JsonDiscriminatorAttribute : Attribute
	{
		/// <summary>
		/// Name to use to discriminate between different classes
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name to use to discriminate between different classes</param>
		public JsonDiscriminatorAttribute(string Name)
		{
			this.Name = Name;
		}
	}

	/// <summary>
	/// Marks a class as the base class of a hierarchy, allowing any known subclasses to be serialized for it.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class JsonKnownTypesAttribute : Attribute
	{
		/// <summary>
		/// Array of derived classes
		/// </summary>
		public Type[] Types { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Types">Array of derived classes</param>
		public JsonKnownTypesAttribute(params Type[] Types)
		{
			this.Types = Types;
		}
	}

	/// <summary>
	/// Serializer for polymorphic objects
	/// </summary>
	public class JsonKnownTypesConverter<T> : JsonConverter<T>
	{
		/// <summary>
		/// Name of the 'Type' field used to store discriminators in the serialized objects
		/// </summary>
		const string TypePropertyName = "Type";

		/// <summary>
		/// Mapping from discriminator to class type
		/// </summary>
		Dictionary<string, Type> DiscriminatorToType;

		/// <summary>
		/// Mapping from class type to discriminator
		/// </summary>
		Dictionary<Type, JsonEncodedText> TypeToDiscriminator;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="KnownTypes">Set of derived class types</param>
		public JsonKnownTypesConverter(IEnumerable<Type> KnownTypes)
		{
			DiscriminatorToType = new Dictionary<string, Type>(StringComparer.OrdinalIgnoreCase);
			foreach (Type KnownType in KnownTypes)
			{
				if (!typeof(T).IsAssignableFrom(KnownType))
				{
					throw new InvalidOperationException($"{KnownType} is not derived from {typeof(T)}");
				}
				foreach (JsonDiscriminatorAttribute DiscriminatorAttribute in KnownType.GetCustomAttributes(typeof(JsonDiscriminatorAttribute), true))
				{
					DiscriminatorToType.Add(DiscriminatorAttribute.Name, KnownType);
				}
			}

			TypeToDiscriminator = DiscriminatorToType.ToDictionary(x => x.Value, x => JsonEncodedText.Encode(x.Key));
		}

		/// <summary>
		/// Determines whether the given type can be converted
		/// </summary>
		/// <param name="TypeToConvert">Type to convert</param>
		/// <returns>True if the type can be converted</returns>
		public override bool CanConvert(Type TypeToConvert)
		{
			return TypeToConvert == typeof(T) || TypeToDiscriminator.ContainsKey(TypeToConvert);
		}

		/// <summary>
		/// Reads an object from the given input stream
		/// </summary>
		/// <param name="Reader">UTF8 reader</param>
		/// <param name="TypeToConvert">Type to be read from the stream</param>
		/// <param name="Options">Options for serialization</param>
		/// <returns>Instance of the serialized object</returns>
		public override T Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
		{
			using (JsonDocument Document = JsonDocument.ParseValue(ref Reader))
			{
				JsonElement Element;
				if (!TryGetPropertyRespectingCase(Document.RootElement, TypePropertyName, Options, out Element))
				{
					throw new JsonException($"Missing '{TypePropertyName}' field for serializing {TypeToConvert.Name}");
				}

				Type? TargetType;
				if (!DiscriminatorToType.TryGetValue(Element.GetString(), out TargetType))
				{
					throw new JsonException($"Invalid '{TypePropertyName}' field ('{Element.GetString()}') for serializing {TypeToConvert.Name}");
				}

				string Text = Document.RootElement.GetRawText();
				T Result = (T)JsonSerializer.Deserialize(Text, TargetType, Options);
				return Result;
			}
		}

		/// <summary>
		/// Finds a property within an object, ignoring case
		/// </summary>
		/// <param name="Object">The object to search</param>
		/// <param name="PropertyName">Name of the property to search</param>
		/// <param name="Options">Options for serialization. The <see cref="JsonSerializerOptions.PropertyNameCaseInsensitive"/> property defines whether a case insensitive scan will be performed.</param>
		/// <param name="OutElement">The property value, if successful</param>
		/// <returns>True if the property was found, false otherwise</returns>
		static bool TryGetPropertyRespectingCase(JsonElement Object, string PropertyName, JsonSerializerOptions Options, out JsonElement OutElement)
		{
			JsonElement NewElement;
			if (Object.TryGetProperty(PropertyName, out NewElement))
			{
				OutElement = NewElement;
				return true;
			}

			if (Options.PropertyNameCaseInsensitive)
			{
				foreach (JsonProperty Property in Object.EnumerateObject())
				{
					if (String.Equals(Property.Name, PropertyName, StringComparison.OrdinalIgnoreCase))
					{
						OutElement = Property.Value;
						return true;
					}
				}
			}

			OutElement = new JsonElement();
			return false;
		}

		/// <summary>
		/// Writes an object to the given output stream
		/// </summary>
		/// <param name="Writer">UTF8 writer</param>
		/// <param name="Value">Object to be serialized to the stream</param>
		/// <param name="Options">Options for serialization</param>
		public override void Write(Utf8JsonWriter Writer, T Value, JsonSerializerOptions Options)
		{
			if (Value == null)
			{
				Writer.WriteNullValue();
			}
			else
			{
				Type ValueType = Value.GetType();

				Writer.WriteStartObject();
				Writer.WriteString(Options.PropertyNamingPolicy?.ConvertName(TypePropertyName) ?? TypePropertyName, TypeToDiscriminator[ValueType]);

				byte[] Bytes = JsonSerializer.SerializeToUtf8Bytes(Value, ValueType, Options);
				using (JsonDocument Document = JsonDocument.Parse(Bytes))
				{
					foreach (JsonProperty Property in Document.RootElement.EnumerateObject())
					{
						Property.WriteTo(Writer);
					}
				}

				Writer.WriteEndObject();
			}
		}
	}

	/// <summary>
	/// Factory for JsonKnownTypes converters
	/// </summary>
	public class JsonKnownTypesConverterFactory : JsonConverterFactory
	{
		/// <summary>
		/// Determines whether it's possible to create a converter for the given type
		/// </summary>
		/// <param name="TypeToConvert">The type being converted</param>
		/// <returns>True if the type can be converted</returns>
		public override bool CanConvert(Type TypeToConvert)
		{
			object[] Attributes = TypeToConvert.GetCustomAttributes(typeof(JsonKnownTypesAttribute), false);
			return Attributes.Length > 0;
		}

		/// <summary>
		/// Creates a converter for the given type
		/// </summary>
		/// <param name="TypeToConvert">The type being converted</param>
		/// <param name="Options">Options for serialization</param>
		/// <returns>New converter for the requested type</returns>
		public override JsonConverter CreateConverter(Type TypeToConvert, JsonSerializerOptions Options)
		{
			object[] Attributes = TypeToConvert.GetCustomAttributes(typeof(JsonKnownTypesAttribute), false);
			Type GenericType = typeof(JsonKnownTypesConverter<>).MakeGenericType(TypeToConvert);
			return (JsonConverter)Activator.CreateInstance(GenericType, Attributes.SelectMany(x => ((JsonKnownTypesAttribute)x).Types))!;
		}
	}
}
