// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Xml;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Utility functions for generating schemas
	/// </summary>
	static class Schemas
	{
		static ConcurrentDictionary<Assembly, XmlDocument?> CachedDocumentation = new ConcurrentDictionary<Assembly, XmlDocument?>();
		static ConcurrentDictionary<Type, JsonSchema> CachedSchemas = new ConcurrentDictionary<Type, JsonSchema>();

		/// <summary>
		/// Create a Json schema (or retrieve a cached schema)
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static JsonSchema CreateSchema(Type Type)
		{
			JsonSchema? Schema;
			if (!CachedSchemas.TryGetValue(Type, out Schema))
			{
				XmlDocument? Documentation;
				if (!CachedDocumentation.TryGetValue(Type.Assembly, out Documentation))
				{
					FileReference InputDocumentationFile = new FileReference(Type.Assembly.Location).ChangeExtension(".xml");
					if (FileReference.Exists(InputDocumentationFile))
					{
						Documentation = new XmlDocument();
						Documentation.Load(InputDocumentationFile.FullName);
					}
					CachedDocumentation.TryAdd(Type.Assembly, Documentation);
				}

				Schema = JsonSchema.FromType(Type, Documentation);
				CachedSchemas.TryAdd(Type, Schema);
			}
			return Schema;
		}
	}
}
