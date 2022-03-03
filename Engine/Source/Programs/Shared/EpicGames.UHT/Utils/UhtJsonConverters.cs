// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Types;
using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// JSON converter to output the source name of a type
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class UhtTypeSourceNameJsonConverter<T> : JsonConverter<T> where T : UhtType
	{
		/// <summary>
		/// Read the JSON value
		/// </summary>
		/// <param name="Reader">Source reader</param>
		/// <param name="TypeToConvert">Type to convert</param>
		/// <param name="Options">Serialization options</param>
		/// <returns>Read value</returns>
		/// <exception cref="NotImplementedException"></exception>
		public override T Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
		{
			throw new NotImplementedException();
		}

		/// <summary>
		/// Write the JSON value
		/// </summary>
		/// <param name="Writer">Destination writer</param>
		/// <param name="Type">Value being written</param>
		/// <param name="Options">Serialization options</param>
		public override void Write(Utf8JsonWriter Writer, T Type, JsonSerializerOptions Options)
		{
			Writer.WriteStringValue(Type.SourceName);
		}
	}

	/// <summary>
	/// Read/Write type source name for an optional type value
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class UhtNullableTypeSourceNameJsonConverter<T> : JsonConverter<T> where T : UhtType
	{
		/// <summary>
		/// Read the JSON value
		/// </summary>
		/// <param name="Reader">Source reader</param>
		/// <param name="TypeToConvert">Type to convert</param>
		/// <param name="Options">Serialization options</param>
		/// <returns>Read value</returns>
		/// <exception cref="NotImplementedException"></exception>
		public override T Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
		{
			throw new NotImplementedException();
		}

		/// <summary>
		/// Write the JSON value
		/// </summary>
		/// <param name="Writer">Destination writer</param>
		/// <param name="Type">Value being written</param>
		/// <param name="Options">Serialization options</param>
		public override void Write(Utf8JsonWriter Writer, T? Type, JsonSerializerOptions Options)
		{
			if (Type != null)
			{
				Writer.WriteStringValue(Type.SourceName);
			}
		}
	}

	/// <summary>
	/// Serialize a list of types
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class UhtTypeListJsonConverter<T> : JsonConverter<List<T>> where T : UhtType
	{
		/// <summary>
		/// Read the JSON value
		/// </summary>
		/// <param name="Reader">Source reader</param>
		/// <param name="TypeToConvert">Type to convert</param>
		/// <param name="Options">Serialization options</param>
		/// <returns>Read value</returns>
		/// <exception cref="NotImplementedException"></exception>
		public override List<T> Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
		{
			throw new NotImplementedException();
		}

		/// <summary>
		/// Write the JSON value
		/// </summary>
		/// <param name="Writer">Destination writer</param>
		/// <param name="Collection">Value being written</param>
		/// <param name="Options">Serialization options</param>
		public override void Write(Utf8JsonWriter Writer, List<T> Collection, JsonSerializerOptions Options)
		{
			Writer.WriteStartArray();
			foreach (UhtType Type in Collection)
			{
				JsonSerializer.Serialize(Writer, Type, Type.GetType(), Options);
			}
			Writer.WriteEndArray();
		}
	}

	/// <summary>
	/// Serialize a nullable list of types
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class UhtNullableTypeListJsonConverter<T> : JsonConverter<List<T>> where T : UhtType
	{
		/// <summary>
		/// Read the JSON value
		/// </summary>
		/// <param name="Reader">Source reader</param>
		/// <param name="TypeToConvert">Type to convert</param>
		/// <param name="Options">Serialization options</param>
		/// <returns>Read value</returns>
		/// <exception cref="NotImplementedException"></exception>
		public override List<T> Read(ref Utf8JsonReader Reader, Type TypeToConvert, JsonSerializerOptions Options)
		{
			throw new NotImplementedException();
		}

		/// <summary>
		/// Write the JSON value
		/// </summary>
		/// <param name="Writer">Destination writer</param>
		/// <param name="Collection">Value being written</param>
		/// <param name="Options">Serialization options</param>
		public override void Write(Utf8JsonWriter Writer, List<T>? Collection, JsonSerializerOptions Options)
		{
			Writer.WriteStartArray();
			if (Collection != null)
			{
				foreach (UhtType Type in Collection)
				{
					JsonSerializer.Serialize(Writer, Type, Type.GetType(), Options);
				}
			}
			Writer.WriteEndArray();
		}
	}
}
