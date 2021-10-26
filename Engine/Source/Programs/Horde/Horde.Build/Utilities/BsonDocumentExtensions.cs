// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Extension methods for BsonDocument
	/// </summary>
	static class BsonDocumentExtensions
	{
		/// <summary>
		/// Gets a property value from a document or subdocument, indicated with dotted notation
		/// </summary>
		/// <param name="Document">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="Type">Expected type of the property</param>
		/// <param name="OutValue">Receives the property value</param>
		/// <returns>True if the property exists and was of the correct type</returns>
		public static bool TryGetPropertyValue(this BsonDocument Document, string Name, BsonType Type, [NotNullWhen(true)] out BsonValue? OutValue)
		{
			int DotIdx = Name.IndexOf('.', StringComparison.Ordinal);
			if (DotIdx == -1)
			{
				return TryGetDirectPropertyValue(Document, Name, Type, out OutValue);
			}

			BsonValue? DocValue;
			if (TryGetDirectPropertyValue(Document, Name.Substring(0, DotIdx), BsonType.Document, out DocValue))
			{
				return TryGetPropertyValue(DocValue.AsBsonDocument, Name.Substring(DotIdx + 1), Type, out OutValue);
			}

			OutValue = null;
			return false;
		}

		/// <summary>
		/// Gets a property value that's an immediate child of the document
		/// </summary>
		/// <param name="Document">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="Type">Expected type of the property</param>
		/// <param name="OutValue">Receives the property value</param>
		/// <returns>True if the property exists and was of the correct type</returns>
		private static bool TryGetDirectPropertyValue(this BsonDocument Document, string Name, BsonType Type, [NotNullWhen(true)] out BsonValue? OutValue)
		{
			BsonValue Value;
			if (Document.TryGetValue(Name, out Value) && Value.BsonType == Type)
			{
				OutValue = Value;
				return true;
			}
			else
			{
				OutValue = null;
				return false;
			}
		}

		/// <summary>
		/// Gets an int32 value from the document
		/// </summary>
		/// <param name="Document">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="OutValue">Receives the property value</param>
		/// <returns>True if the property was retrieved</returns>
		public static bool TryGetInt32(this BsonDocument Document, string Name, out int OutValue)
		{
			BsonValue? Value;
			if (Document.TryGetPropertyValue(Name, BsonType.Int32, out Value))
			{
				OutValue = Value.AsInt32;
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
		/// <param name="Document">Document to get a property for</param>
		/// <param name="Name">Name of the property</param>
		/// <param name="OutValue">Receives the property value</param>
		/// <returns>True if the property was retrieved</returns>
		public static bool TryGetString(this BsonDocument Document, string Name, [NotNullWhen(true)] out string? OutValue)
		{
			BsonValue? Value;
			if (Document.TryGetPropertyValue(Name, BsonType.String, out Value))
			{
				OutValue = Value.AsString;
				return true;
			}
			else
			{
				OutValue = null;
				return false;
			}
		}
	}
}
