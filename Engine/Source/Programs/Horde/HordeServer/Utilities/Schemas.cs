// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Json.Schema;
using Json.Schema.Generation;
using Json.Schema.Generation.Intents;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Xml;

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

		class CamelCaseRefiner : ISchemaRefiner
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

		class DescriptionIntent : ISchemaKeywordIntent
		{
			string Description;

			public DescriptionIntent(string Description)
			{
				this.Description = Description;
			}

			public void Apply(JsonSchemaBuilder Builder)
			{
				Builder.Description(Description);
			}

			public override bool Equals(object? Obj)
			{
				return (Obj is DescriptionIntent Other) && Other.Description.Equals(Description, StringComparison.Ordinal);
			}

			public override int GetHashCode()
			{
				return Description.GetHashCode(StringComparison.Ordinal);
			}
		}
	
		[AttributeUsage(AttributeTargets.Class)]
		class XmlDocAttribute : Attribute, IAttributeHandler
		{
			public string Selector;
			public string Description;

			public XmlDocAttribute(string Selector, string Description)
			{
				this.Selector = Selector;
				this.Description = Description;
			}

			public void AddConstraints(SchemaGeneratorContext Context)
			{
				int Index = Math.Min(1, Context.Intents.Count);
				Context.Intents.Insert(Index, new DescriptionIntent(Description));
			}
		}

		class XmlDocRefiner : ISchemaRefiner
		{
			XmlDocument XmlDoc;

			public XmlDocRefiner(XmlDocument XmlDoc)
			{
				this.XmlDoc = XmlDoc;
			}

			public void Run(SchemaGeneratorContext Context)
			{
				PropertiesIntent? Intent = Context.Intents.OfType<PropertiesIntent>().FirstOrDefault();
				if (Intent != null)
				{
					KeyValuePair<string, SchemaGeneratorContext>[] Properties = Intent.Properties.ToArray();
					foreach ((string PropertyName, SchemaGeneratorContext PropertyContext) in Properties)
					{
						string Selector = $"//member[@name='P:{Context.Type.FullName}.{PropertyName}']/summary";

						XmlNode Node = XmlDoc.SelectSingleNode(Selector);
						if (Node != null)
						{
							string Description = Node.InnerText.Trim().Replace("\r\n", "\n", StringComparison.Ordinal);

							List<Attribute> Attributes = new List<Attribute>(PropertyContext.Attributes);
							Attributes.Add(new XmlDocAttribute(Selector, Description));

							SchemaGeneratorContext NewContext = SchemaGenerationContextCache.Get(PropertyContext.Type, Attributes, PropertyContext.Configuration);
							Intent.Properties[PropertyName] = NewContext;
						}
					}
				}
			}

			public bool ShouldRun(SchemaGeneratorContext Context)
			{
				return Context.Intents.OfType<PropertiesIntent>().Any();
			}
		}

		class LegacyIdKeywordJsonConverter : JsonConverter<LegacyIdKeyword>
		{
			public override LegacyIdKeyword Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
			{
				throw new NotImplementedException();
			}

			public override void Write(Utf8JsonWriter writer, LegacyIdKeyword value, JsonSerializerOptions options)
			{
				writer.WriteString("id", value.Id);
			}
		}

		[SchemaKeyword("id")]
		[JsonConverter(typeof(LegacyIdKeywordJsonConverter))]
		class LegacyIdKeyword : IJsonSchemaKeyword, IEquatable<LegacyIdKeyword>
		{
			public string Id { get; set; }

			public LegacyIdKeyword(string Id)
			{
				this.Id = Id;
			}

			public bool Equals(LegacyIdKeyword? other)
			{
				return other != null && other.Id.Equals(Id, StringComparison.Ordinal);
			}

			public override bool Equals(object? obj)
			{
				return Equals(obj as LegacyIdKeyword);
			}

			public override int GetHashCode()
			{
				return Id.GetHashCode(StringComparison.Ordinal);
			}

			public void Validate(ValidationContext context)
			{
			}
		}

		/// <summary>
		/// Cache of generated schemas
		/// </summary>
		static ConcurrentDictionary<Type, JsonSchema> CachedSchemas = new ConcurrentDictionary<Type, JsonSchema>();

		/// <summary>
		/// Xml documentation for this assembly
		/// </summary>
		static XmlDocument? XmlDoc { get; } = ReadXmlDocumentation();

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
				Config.Refiners.Add(new NoAdditionalPropertiesRefiner());
				if (XmlDoc != null)
				{
					Config.Refiners.Add(new XmlDocRefiner(XmlDoc));
				}
				Config.Refiners.Add(new CamelCaseRefiner());

				JsonSchemaBuilder Builder = new JsonSchemaBuilder()
					.Schema("http://json-schema.org/draft-04/schema#"); // VS2019 only supports draft-4 :(
				Builder.Add(new LegacyIdKeyword(SchemaAttribute.Id));
				Schema = Builder
					.FromType(Type, Config)
					.Build();

				CachedSchemas.TryAdd(Type, Schema);
			}
			return Schema;
		}

		static XmlDocument? ReadXmlDocumentation()
		{
			// Get the path to the XML documentation
			FileReference InputDocumentationFile = new FileReference(Assembly.GetExecutingAssembly().Location).ChangeExtension(".xml");
			if (!FileReference.Exists(InputDocumentationFile))
			{
				return null;
			}

			// Read the documentation
			XmlDocument InputDocumentation = new XmlDocument();
			InputDocumentation.Load(InputDocumentationFile.FullName);
			return InputDocumentation;
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
