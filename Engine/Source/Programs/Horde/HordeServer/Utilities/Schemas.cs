// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using Json.Schema;
using Json.Schema.Generation;
using Json.Schema.Generation.Intents;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Information about a type to generate a schema for
	/// </summary>
	class SchemaInfo
	{
		/// <summary>
		/// Identifier for the schema
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the type
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The type to generate a schema for
		/// </summary>
		public Type Type { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id"></param>
		/// <param name="Name"></param>
		/// <param name="Type"></param>
		public SchemaInfo(string Id, string Name, Type Type)
		{
			this.Id = Id;
			this.Name = Name;
			this.Type = Type;
		}
	}

	/// <summary>
	/// Utility functions for generating schemas
	/// </summary>
	static class Schemas
	{
		class CamelCaseSchemaRefiner : ISchemaRefiner
		{
			public void Run(SchemaGeneratorContext Context)
			{
				PropertiesIntent? Intent = Context.Intents.OfType<PropertiesIntent>().FirstOrDefault();
				if (Intent != null)
				{
					Dictionary<string, SchemaGeneratorContext> Properties = new Dictionary<string, SchemaGeneratorContext>(Intent.Properties);
					foreach ((string Name, SchemaGeneratorContext PropertyContext) in Properties)
					{
						string NewName = Char.ToLowerInvariant(Name[0]) + Name.Substring(1);
						Intent.Properties.Remove(Name);
						Intent.Properties[NewName] = PropertyContext;
					}
				}

			}

			public bool ShouldRun(SchemaGeneratorContext Context)
			{
				return Context.Intents.OfType<PropertiesIntent>().Any();
			}
		}

		static ConcurrentDictionary<(string, Type), JsonSchema> CachedSchemas = new ConcurrentDictionary<(string, Type), JsonSchema>();

		/// <summary>
		/// Creates a schema for the given type
		/// </summary>
		/// <param name="Id"></param>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static JsonSchema CreateSchema(string Id, Type Type)
		{
			JsonSchema? Schema;
			if (!CachedSchemas.TryGetValue((Id, Type), out Schema))
			{
				SchemaGeneratorConfiguration Config = new SchemaGeneratorConfiguration();
				Config.PropertyOrder = PropertyOrder.AsDeclared;
				Config.Refiners.Add(new CamelCaseSchemaRefiner());

				JsonSchemaBuilder Builder = new JsonSchemaBuilder().FromType(Type, Config);
				Builder.Id(Id);

				Schema = Builder.Build();
				CachedSchemas.TryAdd((Id, Type), Schema);
			}
			return Schema;
		}

		/// <summary>
		/// Writes a schema to a source file
		/// </summary>
		/// <param name="Id"></param>
		/// <param name="Type"></param>
		/// <param name="OutputFile"></param>
		public static void WriteSchema(string Id, Type Type, FileReference OutputFile)
		{
			JsonSchema Schema = CreateSchema(Id, Type);

			JsonSerializerOptions Options = new JsonSerializerOptions();
			Options.WriteIndented = true;
			Options.PropertyNamingPolicy = JsonNamingPolicy.CamelCase;
			Options.DictionaryKeyPolicy = JsonNamingPolicy.CamelCase;

			byte[] Result = JsonSerializer.SerializeToUtf8Bytes(Schema, Options);
			FileReference.WriteAllBytes(OutputFile, Result);
		}
	}
}
