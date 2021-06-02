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
using System.Reflection;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Attribute setting the identifier for a schema
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	class JsonSchemaAttribute : Attribute
	{
		/// <summary>
		/// The schema identifier
		/// </summary>
		public string Id { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id"></param>
		public JsonSchemaAttribute(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Attribute setting catalog entries for a schema
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class JsonSchemaCatalogAttribute : Attribute
	{
		/// <summary>
		/// The schema name
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Description of the schema
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Description"></param>
		public JsonSchemaCatalogAttribute(string Name, string Description)
		{
			this.Name = Name;
			this.Description = Description;
		}
	}

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
		/// Description of the schema
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// The type to generate a schema for
		/// </summary>
		public Type Type { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id"></param>
		/// <param name="Name"></param>
		/// <param name="Description"></param>
		/// <param name="Type"></param>
		public SchemaInfo(string Id, string Name, string Description, Type Type)
		{
			this.Id = Id;
			this.Name = Name;
			this.Description = Description;
			this.Type = Type;
		}
	}

	/// <summary>
	/// Utility functions for generating schemas
	/// </summary>
	static class Schemas
	{
		class NoAdditionalPropertiesIntent : ISchemaKeywordIntent
		{
			public void Apply(JsonSchemaBuilder builder)
			{
				builder.AdditionalProperties(false);
			}
		}

		class NoAdditionalPropertiesRefiner : ISchemaRefiner
		{
			public void Run(SchemaGeneratorContext Context)
			{
				Context.Intents.Add(new NoAdditionalPropertiesIntent());
			}

			public bool ShouldRun(SchemaGeneratorContext Context)
			{
				return Context.Intents.OfType<PropertiesIntent>().Any();
			}
		}

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

		/// <summary>
		/// Cache of generated schemas
		/// </summary>
		static ConcurrentDictionary<Type, JsonSchema> CachedSchemas = new ConcurrentDictionary<Type, JsonSchema>();

		/// <summary>
		/// Creates a schema for the given type
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static JsonSchema CreateSchema(Type Type)
		{
			JsonSchema? Schema;
			if (!CachedSchemas.TryGetValue(Type, out Schema))
			{
				JsonSchemaAttribute SchemaAttribute = Type.GetCustomAttribute<JsonSchemaAttribute>()!;

				SchemaGeneratorConfiguration Config = new SchemaGeneratorConfiguration();
				Config.PropertyOrder = PropertyOrder.AsDeclared;
				Config.Generators.Add(new StringIdSchemaGenerator());
				Config.Refiners.Add(new CamelCaseSchemaRefiner());
				Config.Refiners.Add(new NoAdditionalPropertiesRefiner());

				Schema = new JsonSchemaBuilder()
					.Schema(MetaSchemas.Draft7Id)
					.Id(SchemaAttribute.Id)
					.FromType(Type, Config)
					.Build();

				CachedSchemas.TryAdd(Type, Schema);
			}
			return Schema;
		}

		/// <summary>
		/// Writes a schema to a source file
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="OutputFile"></param>
		public static void WriteSchema(Type Type, FileReference OutputFile)
		{
			JsonSchema Schema = CreateSchema(Type);

			JsonSerializerOptions Options = new JsonSerializerOptions();
			Options.WriteIndented = true;
			Options.PropertyNamingPolicy = JsonNamingPolicy.CamelCase;
			Options.DictionaryKeyPolicy = JsonNamingPolicy.CamelCase;

			byte[] Result = JsonSerializer.SerializeToUtf8Bytes(Schema, Options);
			FileReference.WriteAllBytes(OutputFile, Result);
		}
	}
}
