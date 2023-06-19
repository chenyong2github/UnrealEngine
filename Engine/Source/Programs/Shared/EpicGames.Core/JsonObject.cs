// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.Json;
using System.Collections.Specialized;
using System.Collections;
using System.IO;
using System.Text;

using SystemJsonObject = System.Text.Json.Nodes.JsonObject;
using SystemJsonNode = System.Text.Json.Nodes.JsonNode;
using SystemJsonValue = System.Text.Json.Nodes.JsonValue;
using SystemJsonArray = System.Text.Json.Nodes.JsonArray;
using System.Text.Encodings.Web;
using System.Text.Unicode;

namespace EpicGames.Core
{
	public static class OrderedDictionaryExtensions
	{
		public static bool TryGetValue(this OrderedDictionary dictionary, object key, [NotNullWhen(true)] out object? value)
		{
			if (dictionary.Contains(key))
			{
				value = dictionary[key]!;
				return true;
			}
			value = null;
			return false;
		}

		private static object? DeepCopyHelper(object? original)
		{
			if (original is null)
			{
				return null;
			}

			if (original is OrderedDictionary dictionary)
			{
				OrderedDictionary copy = new OrderedDictionary();
				foreach (DictionaryEntry entry in dictionary)
				{
					copy.Add(entry.Key, DeepCopyHelper(entry.Value));
				}
				return copy;
			}

			if (original is Array array)
			{
				Array copy = Array.CreateInstance(array.GetType().GetElementType()!, array.Length);
				for (int index = 0; index < array.Length; ++index)
				{
					copy.SetValue(DeepCopyHelper(array.GetValue(index)), index);
				}
				return copy;
			}
			// It's a value type or a string, it's safe to just copy it 
			return original;
		}

		public static OrderedDictionary CreateDeepCopy(this OrderedDictionary dictionary)
		{
			OrderedDictionary copy = new OrderedDictionary();
			foreach (DictionaryEntry entry in dictionary)
			{
				copy.Add(entry.Key, DeepCopyHelper(entry.Value));
			}
			return copy;
		}
	}

	/// <summary>
	/// Exception thrown for errors parsing JSON files
	/// </summary>
	public class JsonParseException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="format">Format string</param>
		/// <param name="args">Optional arguments</param>
		public JsonParseException(string format, params object[] args)
			: base(String.Format(format, args))
		{
		}
	}

	/// <summary>
	/// Stores a JSON object in memory
	/// </summary>
	public class JsonObject
	{
		readonly OrderedDictionary _rawOrderedObject;

		public JsonObject()
		{
			_rawOrderedObject = new OrderedDictionary();
		}

		/// <summary>
		/// Construct a JSON object from the OrderedDictionary obtained from reading a file on disk or parsing valid json text.
		/// </summary>
		/// <param name="inRawObject">Raw object parsed from disk</param>
		private JsonObject(OrderedDictionary inRawObject)
		{
			_rawOrderedObject = inRawObject.CreateDeepCopy();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="element"></param>
		public JsonObject(JsonElement element)
		{
			_rawOrderedObject = new OrderedDictionary();
			foreach (JsonProperty property in element.EnumerateObject())
			{
				_rawOrderedObject[property.Name] = ParseElement(property.Value);
			}
		}

		public override bool Equals(object? obj)
		{
			if (obj is null || GetType() != obj.GetType())
			{
				return false;
			}
			JsonObject jsonObject = (JsonObject) obj;
			if (_rawOrderedObject.Count != jsonObject._rawOrderedObject.Count)
			{
				return false;
			}
			
			foreach (DictionaryEntry entry in _rawOrderedObject)
			{
				if (!jsonObject._rawOrderedObject.Contains(entry.Key))
				{
					return false;
				}
				if (!entry.Value!.Equals(jsonObject._rawOrderedObject[entry.Key!]))
				{
					return false;
				}
			}

			return true;
		}

		public override int GetHashCode()
		{
			return HashCode.Combine(_rawOrderedObject);
		}

		/// <summary>
		/// Parse an individual element
		/// </summary>
		/// <param name="element"></param>
		/// <returns></returns>
		public static object? ParseElement(JsonElement element)
		{
			switch(element.ValueKind)
			{
				case JsonValueKind.Array:
					return element.EnumerateArray().Select(x => ParseElement(x)).ToArray();
				case JsonValueKind.Number:
					return element.GetDouble();
				case JsonValueKind.Object:
					OrderedDictionary dictionary = new OrderedDictionary();
					foreach (JsonProperty property in element.EnumerateObject())
						{
						dictionary.Add(property.Name, ParseElement(property.Value));
					}
					return dictionary;
				case JsonValueKind.String:
					return element.GetString();
				case JsonValueKind.False:
					return false;
				case JsonValueKind.True:
					return true;
				case JsonValueKind.Null:
					return null;
				default:
					throw new NotImplementedException();
			}
		}

		private SystemJsonNode ToJsonNode(object? obj)
		{
			// All values in the JsonObject are either parsed from a string, read from a file or set/added with the API
			// All values at this point must be supported and the correct types 
			switch (obj)
			{
				case OrderedDictionary objDictionary:
					SystemJsonObject dictionaryJson= new SystemJsonObject();
					foreach (object? key in objDictionary.Keys)
					{
						dictionaryJson.Add((string) key, ToJsonNode(objDictionary[key]));
					}
					return dictionaryJson;
				case Array objArray:
					SystemJsonArray tempJsonArray = new SystemJsonArray();
					foreach (object? element in objArray)
					{
						tempJsonArray.Add(ToJsonNode(element));
					}
					return tempJsonArray;
				default:
					// We support null as per the json spec 
					return SystemJsonValue.Create(obj)!;
			}
		}

		public SystemJsonObject ToSystemJsonObject()
		{
			SystemJsonObject returnObject = new SystemJsonObject();
			foreach (DictionaryEntry entry in _rawOrderedObject )
			{
				returnObject.Add((string) entry.Key, ToJsonNode(entry.Value));
			}
			return returnObject;
		}

		/// <summary>
		/// Read a JSON file from disk and construct a JsonObject from it
		/// </summary>
		/// <param name="file">File to read from</param>
		/// <returns>New JsonObject instance</returns>
		public static JsonObject Read(FileReference file)
		{
			string text = FileReference.ReadAllText(file);
			try
			{
				return Parse(text);
			}
			catch(Exception ex)
			{
				throw new JsonParseException("Unable to parse {0}: {1}", file, ex.Message);
			}
		}

		/// <summary>
		/// Tries to read a JSON file from disk
		/// </summary>
		/// <param name="fileName">File to read from</param>
		/// <param name="result">On success, receives the parsed object</param>
		/// <returns>True if the file was read, false otherwise</returns>
		public static bool TryRead(FileReference fileName, [NotNullWhen(true)] out JsonObject? result)
		{
			if (!FileReference.Exists(fileName))
			{
				result = null;
				return false;
			}

			string text = FileReference.ReadAllText(fileName);
			return TryParse(text, out result);
		}

		/// <summary>
		/// Parse a JsonObject from the given raw text string
		/// </summary>
		/// <param name="text">The text to parse</param>
		/// <returns>New JsonObject instance</returns>
		public static JsonObject Parse(string text)
		{
			try
			{
				JsonDocument document = JsonDocument.Parse(text, new JsonDocumentOptions { AllowTrailingCommas = true });
				return new JsonObject(document.RootElement);
			}
			catch (Exception ex)
			{
				throw new JsonParseException("Failed to parse json text '{0}'. {1}", text, ex.Message);
			}
		}

		/// <summary>
		/// Try to parse a JsonObject from the given raw text string
		/// </summary>
		/// <param name="text">The text to parse</param>
		/// <param name="result">On success, receives the new JsonObject</param>
		/// <returns>True if the object was parsed</returns>
		public static bool TryParse(string text, [NotNullWhen(true)] out JsonObject? result)
		{
			try
			{
				result = Parse(text);
				return true;
			}
			catch (Exception)
			{
				result = null;
				return false;
			}
		}

		/// <summary>
		/// List of key names in this object
		/// </summary>
		public IEnumerable<string> KeyNames => _rawOrderedObject.Keys.Cast<string>();

		/// <summary>
		/// Gets a string field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public string GetStringField(string fieldName)
		{
			string? stringValue;
			if (!TryGetStringField(fieldName, out stringValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return stringValue;
		}

		/// <summary>
		/// Tries to read a string field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetStringField(string fieldName, [NotNullWhen(true)] out string? result)
		{
			object? rawValue;
			if (_rawOrderedObject.TryGetValue(fieldName, out rawValue) && (rawValue is string strValue))
			{
				result = strValue;
				return true;
			}
			else
			{
				result = null;
				return false;
			}
		}

		/// <summary>
		/// Gets a string array field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public string[] GetStringArrayField(string fieldName)
		{
			string[]? stringValues;
			if (!TryGetStringArrayField(fieldName, out stringValues))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return stringValues;
		}

		/// <summary>
		/// Tries to read a string array field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetStringArrayField(string fieldName, [NotNullWhen(true)] out string[]? result)
		{
			object? rawValue;
			if (_rawOrderedObject.TryGetValue(fieldName, out rawValue) && (rawValue is IEnumerable<object> enumValue) && enumValue.All(x => x is string))
			{
				result = enumValue.Select(x => (string)x).ToArray();
				return true;
			}
			else
			{
				result = null;
				return false;
			}
		}

		/// <summary>
		/// Gets a boolean field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public bool GetBoolField(string fieldName)
		{
			bool boolValue;
			if (!TryGetBoolField(fieldName, out boolValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return boolValue;
		}

		/// <summary>
		/// Tries to read a bool field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetBoolField(string fieldName, out bool result)
		{
			object? rawValue;
			if (_rawOrderedObject.TryGetValue(fieldName, out rawValue) && (rawValue is bool boolValue))
			{
				result = boolValue;
				return true;
			}
			else
			{
				result = false;
				return false;
			}
		}

		/// <summary>
		/// Gets an integer field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public int GetIntegerField(string fieldName)
		{
			int integerValue;
			if (!TryGetIntegerField(fieldName, out integerValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return integerValue;
		}

		/// <summary>
		/// Tries to read an integer field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetIntegerField(string fieldName, out int result)
		{
			object? rawValue;
			if (!_rawOrderedObject.TryGetValue(fieldName, out rawValue) || !Int32.TryParse(rawValue?.ToString(), out result))
			{
				result = 0;
				return false;
			}
			return true;
		}

		/// <summary>
		/// Tries to read an unsigned integer field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetUnsignedIntegerField(string fieldName, out uint result)
		{
			object? rawValue;
			if (!_rawOrderedObject.TryGetValue(fieldName, out rawValue) || !UInt32.TryParse(rawValue?.ToString(), out result))
			{
				result = 0;
				return false;
			}
			return true;
		}

		/// <summary>
		/// Gets a double field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public double GetDoubleField(string fieldName)
		{
			double doubleValue;
			if (!TryGetDoubleField(fieldName, out doubleValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return doubleValue;
		}

		/// <summary>
		/// Tries to read a double field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetDoubleField(string fieldName, out double result)
		{
			object? rawValue;
			if (!_rawOrderedObject.TryGetValue(fieldName, out rawValue) || !Double.TryParse(rawValue?.ToString(), out result))
			{
				result = 0.0;
				return false;
			}
			return true;
		}

		/// <summary>
		/// Gets an enum field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public T GetEnumField<T>(string fieldName) where T : struct
		{
			T enumValue;
			if (!TryGetEnumField(fieldName, out enumValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return enumValue;
		}

		/// <summary>
		/// Tries to read an enum field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetEnumField<T>(string fieldName, out T result) where T : struct
		{
			string? stringValue;
			if (!TryGetStringField(fieldName, out stringValue) || !Enum.TryParse<T>(stringValue, true, out result))
			{
				result = default;
				return false;
			}
			return true;
		}

		/// <summary>
		/// Tries to read an enum array field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetEnumArrayField<T>(string fieldName, [NotNullWhen(true)] out T[]? result) where T : struct
		{
			string[]? stringValues;
			if (!TryGetStringArrayField(fieldName, out stringValues))
			{
				result = null;
				return false;
			}

			T[] enumValues = new T[stringValues.Length];
			for (int idx = 0; idx < stringValues.Length; idx++)
			{
				if (!Enum.TryParse<T>(stringValues[idx], true, out enumValues[idx]))
				{
					result = null;
					return false;
				}
			}

			result = enumValues;
			return true;
		}

		/// <summary>
		/// Gets an object field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public JsonObject GetObjectField(string fieldName)
		{
			JsonObject? result;
			if (!TryGetObjectField(fieldName, out result))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return result;
		}

		/// <summary>
		/// Tries to read an object field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetObjectField(string fieldName, [NotNullWhen(true)] out JsonObject? result)
		{
			object? rawValue;
			if (_rawOrderedObject.TryGetValue(fieldName, out rawValue) && (rawValue is OrderedDictionary orderedDictValue))
			{
				result = new JsonObject(orderedDictValue);
				return true;
			}
			else
			{
				result = null;
				return false;
			}
		}

		/// <summary>
		/// Gets an object array field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public JsonObject[] GetObjectArrayField(string fieldName)
		{
			JsonObject[]? result;
			if (!TryGetObjectArrayField(fieldName, out result))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", fieldName);
			}
			return result;
		}

		/// <summary>
		/// Tries to read an object array field by the given name from the object
		/// </summary>
		/// <param name="fieldName">Name of the field to get</param>
		/// <param name="result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetObjectArrayField(string fieldName, [NotNullWhen(true)] out JsonObject[]? result)
		{
			object? rawValue;
			if (_rawOrderedObject.TryGetValue(fieldName, out rawValue) && (rawValue is IEnumerable<object> enumValue) && enumValue.All(x => x is OrderedDictionary))
			{
				result = enumValue.Select(x => new JsonObject((OrderedDictionary)x)).ToArray();
				return true;
			}
			else
			{
				result = null;
				return false;
			}
		}

		/// <summary>
		/// Checks if the provided field exists in this Json object.
		/// </summary>
		/// <param name="fieldName">Name of the field to check if it is contained. </param>
		/// <returns>True if the field exists in the JsonObject, false otherwise</returns>
		public bool ContainsField(string fieldName)
		{
			return _rawOrderedObject.Contains(fieldName);
		}

		public void AddOrSetFieldValue(string fieldName, int value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			_rawOrderedObject[fieldName] = Convert.ToDouble(value);
		}

		public void AddOrSetFieldValue(string fieldName, uint value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			_rawOrderedObject[fieldName] = Convert.ToDouble(value);
		}

		public void AddOrSetFieldValue(string fieldName, double value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			_rawOrderedObject[fieldName] = value;
		}

		public void AddOrSetFieldValue(string fieldName, bool value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			_rawOrderedObject[fieldName] = value;
		}

		public void AddOrSetFieldValue(string fieldName, string? value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			// We set null strings to empty strings to follow the behavior of EpicGames.Core.JsonWriter
			value ??= "";
			_rawOrderedObject[fieldName] = value;
		}

		public void AddOrSetFieldValue<T>(string fieldName, T value) where T : Enum
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			
			_rawOrderedObject[fieldName] = value.ToString();
		}

		
		public void AddOrSetFieldValue(string fieldName, JsonObject? value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}

			_rawOrderedObject[fieldName] = value?._rawOrderedObject.CreateDeepCopy();
		}

		public void AddOrSetFieldValue(string fieldName, string?[]? value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			if (value is null)
			{
				_rawOrderedObject[fieldName] = value;
				return;
			}
			// Contents of the strings array should never be null and should be "" instead to match behavior EpicGames.Core.JsonWriter
			string[] stringArray = value.Select(x => x ?? "").ToArray();
			_rawOrderedObject[fieldName] = stringArray;
		}

		public void AddOrSetFieldValue(string fieldName, JsonObject[]? value)
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			if (value is null)
			{
				_rawOrderedObject[fieldName] = null;
				return;
			}
			List<OrderedDictionary?> objList = new List<OrderedDictionary?>();
			foreach (JsonObject? obj in value)
			{
				objList.Add(obj?._rawOrderedObject.CreateDeepCopy());
			}
			_rawOrderedObject[fieldName] = objList.ToArray();
		}

		public void AddOrSetFieldValue<T>(string fieldName, T[]? value) where T : Enum
		{
			if (String.IsNullOrEmpty(fieldName))
			{
				return;
			}
			if (value is null)
			{
				_rawOrderedObject[fieldName] = null;
				return;
			}

			string[] stringArray = value.Select(x => x.ToString()!).ToArray();
			_rawOrderedObject[fieldName] = stringArray;
		}
		/// <summary>
		/// Converts this Json Object to a string representation. This follows formatting of UE .uplugin files with 4 spaces for tabs and indentation enabled.
		/// IMPORTANT: If this JsonObject contains HTML, the returned string should NOT be used directly for HTML or a script. Read the note below.
		/// </summary>
		/// <returns>The formatted, prettified string representation of this Json Object.</returns>
		public string ToJsonString()
		{
			SystemJsonObject jsonObjectToWrite = ToSystemJsonObject();
			JsonWriterOptions options = new JsonWriterOptions();
			options.Indented = true;
			// IMPORTANT: Utf8JsonWriter blocks certain characters like +, &, <, >,` from being escaped in a global block list
			// Best way around is to use the relaxed JavaScriptEncoder.UnsafeRelaxedJsonEscaping.
			// However this can have security implications if this string is ever written to an HTML page or script
			// https://learn.microsoft.com/en-us/dotnet/standard/serialization/system-text-json/character-encoding
			options.Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping;
			using MemoryStream jsonMemoryStream = new MemoryStream();
			using (Utf8JsonWriter writer = new Utf8JsonWriter(jsonMemoryStream, options))
			{
				jsonObjectToWrite.WriteTo(writer);
				writer.Flush();
			}
			// The Utf8JsonWriter doesn't format json the same as we want in UE, we massage the string to meet our standards 
			string jsonString = Encoding.UTF8.GetString(jsonMemoryStream.ToArray());
			string[] lines = jsonString.Split(new[] { Environment.NewLine }, StringSplitOptions.None);
			StringBuilder jsonStringBuilder = new StringBuilder();
			foreach (string line in lines)
			{
				// Utf8JsonWriter uses 2 spaces for indents, we replace them with tabs here 
				int numLeadingSpaces = line.TakeWhile(x => x == ' ').Count();
				int numLeadingTabs = numLeadingSpaces / 2;
				jsonStringBuilder.Append('\t', numLeadingTabs);
				jsonStringBuilder.AppendLine(line.Substring(numLeadingSpaces));
			}

			return jsonStringBuilder.ToString();
		}
	}
}
