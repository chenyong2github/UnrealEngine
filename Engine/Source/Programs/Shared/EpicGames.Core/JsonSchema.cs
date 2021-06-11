// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using System.Xml;

namespace EpicGames.Core
{
	/// <summary>
	/// Interface used to write JSON schema types. This is abstracted to allow multiple passes over the document structure, in order to optimize multiple references to the same type definition.
	/// </summary>
	public interface IJsonSchemaWriter
	{
		/// <inheritdoc cref="Utf8JsonWriter.WriteStartObject()"/>
		void WriteStartObject();

		/// <inheritdoc cref="Utf8JsonWriter.WriteStartObject(string)"/>
		void WriteStartObject(string Name);

		/// <inheritdoc cref="Utf8JsonWriter.WriteEndObject()"/>
		void WriteEndObject();

		/// <inheritdoc cref="Utf8JsonWriter.WriteStartArray()"/>
		void WriteStartArray();

		/// <inheritdoc cref="Utf8JsonWriter.WriteStartArray(string)"/>
		void WriteStartArray(string Name);

		/// <inheritdoc cref="Utf8JsonWriter.WriteEndObject()"/>
		void WriteEndArray();

		/// <inheritdoc cref="Utf8JsonWriter.WriteBoolean(string, bool)"/>
		void WriteBoolean(string Name, bool Value);

		/// <inheritdoc cref="Utf8JsonWriter.WriteString(string, string)"/>
		void WriteString(string Key, string Value);

		/// <inheritdoc cref="Utf8JsonWriter.WriteStringValue(string)"/>
		void WriteStringValue(string Name);

		/// <summary>
		/// Serialize a type to JSON
		/// </summary>
		/// <param name="Type"></param>
		void WriteType(JsonSchemaType Type);
	}

	/// <summary>
	/// Implementation of a JSON schema. Implements draft 04 (latest supported by Visual Studio 2019).
	/// </summary>
	public class JsonSchema
	{
		class JsonTypeRefCollector : IJsonSchemaWriter
		{
			/// <summary>
			/// Reference counts for each type (max of 2)
			/// </summary>
			public Dictionary<JsonSchemaType, int> TypeRefCount { get; } = new Dictionary<JsonSchemaType, int>();

			/// <inheritdoc/>
			public void WriteBoolean(string Name, bool Value) { }

			/// <inheritdoc/>
			public void WriteString(string Key, string Value) { }

			/// <inheritdoc/>
			public void WriteStringValue(string Name) { }

			/// <inheritdoc/>
			public void WriteStartObject() { }

			/// <inheritdoc/>
			public void WriteStartObject(string Name) { }

			/// <inheritdoc/>
			public void WriteEndObject() { }

			/// <inheritdoc/>
			public void WriteStartArray() { }

			/// <inheritdoc/>
			public void WriteStartArray(string Name) { }

			/// <inheritdoc/>
			public void WriteEndArray() { }

			/// <inheritdoc/>
			public void WriteType(JsonSchemaType Type)
			{
				if (!(Type is JsonSchemaPrimitiveType))
				{
					TypeRefCount.TryGetValue(Type, out int RefCount);
					if (RefCount < 2)
					{
						TypeRefCount[Type] = ++RefCount;
					}
					if (RefCount < 2)
					{
						Type.Write(this);
					}
				}
			}
		}

		/// <summary>
		/// Implementation of <see cref="IJsonSchemaWriter"/>
		/// </summary>
		class JsonSchemaWriter : IJsonSchemaWriter
		{
			/// <summary>
			/// Raw Json output
			/// </summary>
			Utf8JsonWriter JsonWriter;

			/// <summary>
			/// Mapping of type to definition name
			/// </summary>
			Dictionary<JsonSchemaType, string> TypeToDefinition { get; }

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Writer"></param>
			/// <param name="TypeToDefinition"></param>
			public JsonSchemaWriter(Utf8JsonWriter Writer, Dictionary<JsonSchemaType, string> TypeToDefinition)
			{
				this.JsonWriter = Writer;
				this.TypeToDefinition = TypeToDefinition;
			}

			/// <inheritdoc/>
			public void WriteBoolean(string Name, bool Value) => JsonWriter.WriteBoolean(Name, Value);

			/// <inheritdoc/>
			public void WriteString(string Key, string Value) => JsonWriter.WriteString(Key, Value);

			/// <inheritdoc/>
			public void WriteStringValue(string Name) => JsonWriter.WriteStringValue(Name);

			/// <inheritdoc/>
			public void WriteStartObject() => JsonWriter.WriteStartObject();

			/// <inheritdoc/>
			public void WriteStartObject(string Name) => JsonWriter.WriteStartObject(Name);

			/// <inheritdoc/>
			public void WriteEndObject() => JsonWriter.WriteEndObject();

			/// <inheritdoc/>
			public void WriteStartArray() => JsonWriter.WriteStartArray();

			/// <inheritdoc/>
			public void WriteStartArray(string Name) => JsonWriter.WriteStartArray(Name);

			/// <inheritdoc/>
			public void WriteEndArray() => JsonWriter.WriteEndArray();

			/// <summary>
			/// Writes a type, either inline or as a reference to a definition elsewhere
			/// </summary>
			/// <param name="Type"></param>
			public void WriteType(JsonSchemaType Type)
			{
				if (TypeToDefinition.TryGetValue(Type, out string? Definition))
				{
					JsonWriter.WriteString("$ref", $"#/definitions/{Definition}");
				}
				else
				{
					Type.Write(this);
				}
			}
		}

		/// <summary>
		/// Identifier for the schema
		/// </summary>
		public string? Id { get; set; }

		/// <summary>
		/// The root schema type
		/// </summary>
		public JsonSchemaType RootType { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Id for the schema</param>
		/// <param name="RootType"></param>
		public JsonSchema(string? Id, JsonSchemaType RootType)
		{
			this.Id = Id;
			this.RootType = RootType;
		}

		/// <summary>
		/// Write this schema to a byte array
		/// </summary>
		/// <param name="Writer"></param>
		public void Write(Utf8JsonWriter Writer)
		{
			// Determine reference counts for each type. Any type referenced at least twice will be split off into a separate definition.
			JsonTypeRefCollector RefCollector = new JsonTypeRefCollector();
			RefCollector.WriteType(RootType);

			// Assign names to each type definition
			HashSet<string> DefinitionNames = new HashSet<string>();
			Dictionary<JsonSchemaType, string> TypeToDefinition = new Dictionary<JsonSchemaType, string>();
			foreach ((JsonSchemaType Type, int RefCount) in RefCollector.TypeRefCount)
			{
				if (RefCount > 1)
				{
					string BaseName = Type.Name ?? "unnamed";

					string Name = BaseName;
					for (int Idx = 1; !DefinitionNames.Add(Name); Idx++)
					{
						Name = $"{BaseName}{Idx}";
					}

					TypeToDefinition[Type] = Name;
				}
			}

			// Write the schema
			Writer.WriteStartObject();
			Writer.WriteString("$schema", "http://json-schema.org/draft-04/schema#");
			if (Id != null)
			{
				Writer.WriteString("$id", Id);
			}

			JsonSchemaWriter SchemaWriter = new JsonSchemaWriter(Writer, TypeToDefinition);
			RootType.Write(SchemaWriter);

			if (TypeToDefinition.Count > 0)
			{
				Writer.WriteStartObject("definitions");
				foreach ((JsonSchemaType Type, string RefName) in TypeToDefinition)
				{
					Writer.WriteStartObject(RefName);
					Type.Write(SchemaWriter);
					Writer.WriteEndObject();
				}
				Writer.WriteEndObject();
			}

			Writer.WriteEndObject();
		}

		/// <summary>
		/// Write this schema to a stream
		/// </summary>
		/// <param name="Stream">The output stream</param>
		public void Write(Stream Stream)
		{
			using (Utf8JsonWriter Writer = new Utf8JsonWriter(Stream, new JsonWriterOptions { Indented = true }))
			{
				Write(Writer);
			}
		}

		/// <summary>
		/// Writes this schema to a file
		/// </summary>
		/// <param name="File">The output file</param>
		public void Write(FileReference File)
		{
			using (FileStream Stream = FileReference.Open(File, FileMode.Create))
			{
				Write(Stream);
			}
		}

		/// <summary>
		/// Constructs a Json schema from a type
		/// </summary>
		/// <param name="Type">The type to construct from</param>
		/// <param name="XmlDoc">C# Xml documentation file, to use for property descriptions</param>
		/// <returns>New schema object</returns>
		public static JsonSchema FromType(Type Type, XmlDocument? XmlDoc)
		{
			JsonSchemaAttribute? SchemaAttribute = Type.GetCustomAttribute<JsonSchemaAttribute>();
			return new JsonSchema(SchemaAttribute?.Id, CreateSchemaType(Type, new Dictionary<Type, JsonSchemaType>(), XmlDoc));
		}

		/// <summary>
		/// Gets the description for a type
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="XmlDoc"></param>
		/// <returns></returns>
		static string? GetTypeDescription(Type Type, XmlDocument? XmlDoc)
		{
			if (XmlDoc == null)
			{
				return null;
			}

			string Selector = $"//member[@name='T:{Type.FullName}']/summary";

			XmlNode Node = XmlDoc.SelectSingleNode(Selector);
			if (Node == null)
			{
				return null;
			}

			return Node.InnerText.Trim().Replace("\r\n", "\n", StringComparison.Ordinal);
		}

		/// <summary>
		/// Gets a property description from Xml documentation file
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Name"></param>
		/// <param name="XmlDoc"></param>
		/// <returns></returns>
		static string? GetPropertyDescription(Type Type, string Name, XmlDocument? XmlDoc)
		{
			if (XmlDoc == null)
			{
				return null;
			}

			string Selector = $"//member[@name='P:{Type.FullName}.{Name}']/summary";

			XmlNode Node = XmlDoc.SelectSingleNode(Selector);
			if (Node == null)
			{
				return null;
			}

			return Node.InnerText.Trim().Replace("\r\n", "\n", StringComparison.Ordinal);
		}

		/// <summary>
		/// Constructs a schema type from the given type object
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="XmlDoc"></param>
		/// <returns></returns>
		static JsonSchemaType CreateSchemaType(Type Type, Dictionary<Type, JsonSchemaType> TypeCache, XmlDocument? XmlDoc)
		{
			switch (Type.GetTypeCode(Type))
			{
				case TypeCode.Boolean:
					return new JsonSchemaBoolean();
				case TypeCode.Byte:
				case TypeCode.SByte:
				case TypeCode.Int16:
				case TypeCode.UInt16:
				case TypeCode.Int32:
				case TypeCode.UInt32:
				case TypeCode.Int64:
				case TypeCode.UInt64:
					return new JsonSchemaInteger();
				case TypeCode.Single:
				case TypeCode.Double:
					return new JsonSchemaNumber();
				case TypeCode.String:
					return new JsonSchemaString();
			}

			JsonSchemaTypeAttribute? Attribute = Type.GetCustomAttribute<JsonSchemaTypeAttribute>();
			switch (Attribute)
			{
				case JsonSchemaStringAttribute String:
					return new JsonSchemaString(String.Format);
			}

			if (Type.IsEnum)
			{
				return new JsonSchemaEnum(Enum.GetNames(Type)) { Name = Type.Name };
			}
			if (Type.IsGenericType && Type.GetGenericTypeDefinition() == typeof(Nullable<>))
			{
				return CreateSchemaType(Type.GetGenericArguments()[0], TypeCache, XmlDoc);
			}
			if (Type == typeof(DateTime) || Type == typeof(DateTimeOffset))
			{
				return new JsonSchemaString(JsonSchemaStringFormat.DateTime);
			}

			Type[] Interfaces = Type.GetInterfaces();
			foreach (Type Interface in Interfaces)
			{
				if (Interface.IsGenericType)
				{
					Type[] Arguments = Interface.GetGenericArguments();
					if (Interface.GetGenericTypeDefinition() == typeof(IList<>))
					{
						return new JsonSchemaArray(CreateSchemaType(Arguments[0], TypeCache, XmlDoc));
					}
					if (Interface.GetGenericTypeDefinition() == typeof(IDictionary<,>))
					{
						JsonSchemaObject Object = new JsonSchemaObject();
						Object.AdditionalProperties = CreateSchemaType(Arguments[1], TypeCache, XmlDoc);
						return Object;
					}
				}
			}

			if (Type.IsClass)
			{
				if (TypeCache.TryGetValue(Type, out JsonSchemaType? SchemaType))
				{
					return SchemaType;
				}

				JsonKnownTypesAttribute? KnownTypes = Type.GetCustomAttribute<JsonKnownTypesAttribute>(false);
				if (KnownTypes != null)
				{
					JsonSchemaOneOf Object = new JsonSchemaOneOf();
					TypeCache[Type] = Object;
					SetOneOfProperties(Object, Type, KnownTypes.Types, TypeCache, XmlDoc);
					return Object;
				}
				else
				{
					JsonSchemaObject Object = new JsonSchemaObject();
					TypeCache[Type] = Object;
					SetObjectProperties(Object, Type, TypeCache, XmlDoc);
					return Object;
				}
			}

			throw new Exception($"Unknown type for schema generation: {Type}");
		}

		static void SetOneOfProperties(JsonSchemaOneOf Object, Type Type, Type[] KnownTypes, Dictionary<Type, JsonSchemaType> TypeCache, XmlDocument? XmlDoc)
		{
			Object.Name = Type.Name;

			foreach (Type KnownType in KnownTypes)
			{
				JsonDiscriminatorAttribute? Attribute = KnownType.GetCustomAttribute<JsonDiscriminatorAttribute>();
				if (Attribute != null)
				{
					JsonSchemaObject KnownObject = new JsonSchemaObject();
					KnownObject.Properties.Add(new JsonSchemaProperty("type", "Type discriminator", new JsonSchemaEnum(new[] { Attribute.Name })));
					SetObjectProperties(KnownObject, KnownType, TypeCache, XmlDoc);
					Object.Types.Add(KnownObject);
				}
			}
		}

		static void SetObjectProperties(JsonSchemaObject Object, Type Type, Dictionary<Type, JsonSchemaType> TypeCache, XmlDocument? XmlDoc)
		{
			Object.Name = Type.Name;

			PropertyInfo[] Properties = Type.GetProperties(BindingFlags.Instance | BindingFlags.Public);
			foreach (PropertyInfo Property in Properties)
			{
				string? Description = GetPropertyDescription(Type, Property.Name, XmlDoc);
				JsonSchemaType PropertyType = CreateSchemaType(Property.PropertyType, TypeCache, XmlDoc);
				Object.Properties.Add(new JsonSchemaProperty(Property.Name, Description, PropertyType));
			}
		}
	}
}
