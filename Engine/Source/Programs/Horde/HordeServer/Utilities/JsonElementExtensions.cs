// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson.IO;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Extensions for parsing values out of generic dictionary objects
	/// </summary>
	static class JsonElementExtensions
	{
		/// <summary>
		/// Checks if the element has a property with the given value
		/// </summary>
		/// <param name="Element">Element to search</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="Value">Expected value of the property</param>
		/// <returns>True if the property exists and matches</returns>
		public static bool HasStringProperty(this JsonElement Element, ReadOnlySpan<byte> Name, string Value)
		{
			JsonElement Property;
			return Element.ValueKind == JsonValueKind.Object && Element.TryGetProperty(Name, out Property) && Property.ValueEquals(Value);
		}

		/// <summary>
		/// Checks if the element has a property with the given value
		/// </summary>
		/// <param name="Element">Element to search</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="Value">Expected value of the property</param>
		/// <returns>True if the property exists and matches</returns>
		public static bool HasStringProperty(this JsonElement Element, string Name, string Value)
		{
			JsonElement Property;
			return Element.ValueKind == JsonValueKind.Object && Element.TryGetProperty(Name, out Property) && Property.ValueEquals(Value);
		}

		/// <summary>
		/// Gets a property value of a certain type
		/// </summary>
		/// <param name="Element">The element to get a property from</param>
		/// <param name="Name">Name of the element</param>
		/// <param name="ValueKind">The required type of property</param>
		/// <param name="Value">Value of the property</param>
		/// <returns>True if the property exists and was a string</returns>
		public static bool TryGetProperty(this JsonElement Element, ReadOnlySpan<byte> Name, JsonValueKind ValueKind, [NotNullWhen(true)] out JsonElement Value)
		{
			JsonElement Property;
			if (Element.TryGetProperty(Name, out Property) && Property.ValueKind == ValueKind)
			{
				Value = Property;
				return true;
			}
			else
			{
				Value = new JsonElement();
				return false;
			}
		}

		/// <summary>
		/// Gets a property value of a certain type
		/// </summary>
		/// <param name="Element">The element to get a property from</param>
		/// <param name="Name">Name of the element</param>
		/// <param name="ValueKind">The required type of property</param>
		/// <param name="Value">Value of the property</param>
		/// <returns>True if the property exists and was a string</returns>
		public static bool TryGetProperty(this JsonElement Element, string Name, JsonValueKind ValueKind, [NotNullWhen(true)] out JsonElement Value)
		{
			JsonElement Property;
			if (Element.TryGetProperty(Name, out Property) && Property.ValueKind == ValueKind)
			{
				Value = Property;
				return true;
			}
			else
			{
				Value = new JsonElement();
				return false;
			}
		}

		/// <summary>
		/// Gets a string property value
		/// </summary>
		/// <param name="Element">The element to get a property from</param>
		/// <param name="Name">Name of the element</param>
		/// <param name="Value">Value of the property</param>
		/// <returns>True if the property exists and was a string</returns>
		public static bool TryGetStringProperty(this JsonElement Element, ReadOnlySpan<byte> Name, [NotNullWhen(true)] out string? Value)
		{
			JsonElement Property;
			if (Element.TryGetProperty(Name, out Property) && Property.ValueKind == JsonValueKind.String)
			{
				Value = Property.GetString()!;
				return true;
			}
			else
			{
				Value = null;
				return false;
			}
		}

		/// <summary>
		/// Gets a string property value
		/// </summary>
		/// <param name="Element">The element to get a property from</param>
		/// <param name="Name">Name of the element</param>
		/// <param name="Value">Value of the property</param>
		/// <returns>True if the property exists and was a string</returns>
		public static bool TryGetStringProperty(this JsonElement Element, string Name, [NotNullWhen(true)] out string? Value)
		{
			return TryGetStringProperty(Element, Encoding.UTF8.GetBytes(Name).AsSpan(), out Value);
		}

		/// <summary>
		/// Gets a property value from a document or subdocument, indicated with dotted notation
		/// </summary>
		/// <param name="Element">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="OutElement">Receives the nexted element</param>
		/// <returns>True if the property exists and was of the correct type</returns>
		public static bool TryGetNestedProperty(this JsonElement Element, ReadOnlySpan<byte> Name, [NotNullWhen(true)] out JsonElement OutElement)
		{
			int DotIdx = Name.IndexOf((byte)'.');
			if (DotIdx == -1)
			{
				return Element.TryGetProperty(Name, out OutElement);
			}

			JsonElement DocValue;
			if (Element.TryGetProperty(Name.Slice(0, DotIdx), out DocValue) && DocValue.ValueKind == JsonValueKind.Object)
			{
				return TryGetNestedProperty(DocValue, Name.Slice(DotIdx + 1), out OutElement);
			}

			OutElement = new JsonElement();
			return false;
		}

		/// <summary>
		/// Gets a property value from a document or subdocument, indicated with dotted notation
		/// </summary>
		/// <param name="Element">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="OutElement">Receives the nexted element</param>
		/// <returns>True if the property exists and was of the correct type</returns>
		public static bool TryGetNestedProperty(this JsonElement Element, string Name, [NotNullWhen(true)] out JsonElement OutElement)
		{
			return TryGetNestedProperty(Element, Encoding.UTF8.GetBytes(Name).AsSpan(), out OutElement);
		}

		/// <summary>
		/// Gets an int32 value from the document
		/// </summary>
		/// <param name="Element">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="OutValue">Receives the property value</param>
		/// <returns>True if the property was retrieved</returns>
		public static bool TryGetNestedProperty(this JsonElement Element, string Name, out int OutValue)
		{
			JsonElement Value;
			if (Element.TryGetNestedProperty(Name, out Value) && Value.ValueKind == JsonValueKind.Number)
			{
				OutValue = Value.GetInt32();
				return true;
			}
			else
			{
				OutValue = 0;
				return false;
			}
		}

		/// <summary>
		/// Gets a string value from the document
		/// </summary>
		/// <param name="Element">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="OutValue">Receives the property value</param>
		/// <returns>True if the property was retrieved</returns>
		public static bool TryGetNestedProperty(this JsonElement Element, string Name, [NotNullWhen(true)] out string? OutValue)
		{
			JsonElement Value;
			if (Element.TryGetNestedProperty(Name, out Value) && Value.ValueKind == JsonValueKind.String)
			{
				OutValue = Value.GetString();
				return OutValue != null;
			}
			else
			{
				OutValue = null;
				return false;
			}
		}
	}
}
