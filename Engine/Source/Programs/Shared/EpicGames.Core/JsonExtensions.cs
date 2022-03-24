// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text.Json;

namespace EpicGames.Core
{
	/// <summary>
	/// Utility methods for utf8 json reader/writer
	/// </summary>
	public static class JsonExtensions
	{
		/// <summary>
		/// Tries to read a property name then move to the value token
		/// </summary>
		/// <param name="reader">Token reader</param>
		/// <param name="propertyName">Receives the property name on success</param>
		/// <returns>True if the read succeeded</returns>
		public static bool TryReadNextPropertyName(ref Utf8JsonReader reader, out ReadOnlySpan<byte> propertyName)
		{
			if (reader.Read() && reader.TokenType == JsonTokenType.PropertyName)
			{
				propertyName = reader.ValueSpan;
				return reader.Read();
			}
			else
			{
				propertyName = ReadOnlySpan<byte>.Empty;
				return false;
			}
		}

		/// <summary>
		/// Reads a Utf8 string from the current json token
		/// </summary>
		/// <param name="reader"></param>
		/// <returns></returns>
		public static Utf8String GetUtf8String(this Utf8JsonReader reader)
		{
			throw new NotImplementedException();
		}
	}
}
