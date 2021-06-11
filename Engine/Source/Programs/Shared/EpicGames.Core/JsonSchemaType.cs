// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Base class for Json schema types
	/// </summary>
	public abstract class JsonSchemaType
	{
		/// <summary>
		/// Name for this type, if stored in a standalone definition
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Write this type to the given archive
		/// </summary>
		/// <param name="Writer"></param>
		public abstract void Write(IJsonSchemaWriter Writer);
	}

	/// <summary>
	/// Base class for types that cannot contain references to other types
	/// </summary>
	public abstract class JsonSchemaPrimitiveType : JsonSchemaType
	{
	}

	/// <summary>
	/// Represents a boolean in a Json schema
	/// </summary>
	public class JsonSchemaBoolean : JsonSchemaPrimitiveType
	{
		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteString("type", "boolean");
		}
	}

	/// <summary>
	/// Represents an integer in a Json schema
	/// </summary>
	public class JsonSchemaInteger : JsonSchemaPrimitiveType
	{
		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteString("type", "integer");
		}
	}

	/// <summary>
	/// Represents a number of any type in a Json schema
	/// </summary>
	public class JsonSchemaNumber : JsonSchemaPrimitiveType
	{
		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteString("type", "integer");
		}
	}

	/// <summary>
	/// Format for a string element schema
	/// </summary>
	public enum JsonSchemaStringFormat
	{
		/// <summary>
		/// A date/time value
		/// </summary>
		DateTime,

		/// <summary>
		/// Internet email address, see RFC 5322, section 3.4.1.
		/// </summary>
		Email,

		/// <summary>
		/// Internet host name, see RFC 1034, section 3.1.
		/// </summary>
		HostName,

		/// <summary>
		/// IPv4 address, according to dotted-quad ABNF syntax as defined in RFC 2673, section 3.2.
		/// </summary>
		Ipv4,

		/// <summary>
		/// IPv6 address, as defined in RFC 2373, section 2.2.
		/// </summary>
		Ipv6,

		/// <summary>
		/// A universal resource identifier (URI), according to RFC3986.
		/// </summary>
		Uri
	}

	/// <summary>
	/// Represents a string in a Json schema
	/// </summary>
	public class JsonSchemaString : JsonSchemaPrimitiveType
	{
		/// <summary>
		/// Optional string describing the formatting of this type
		/// </summary>
		public JsonSchemaStringFormat? Format { get; set; }

		/// <summary>
		/// Pattern for matched strings
		/// </summary>
		public string? Pattern { get; set; } 

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Format"></param>
		public JsonSchemaString(JsonSchemaStringFormat? Format = null, string? Pattern = null)
		{
			this.Format = Format;
			this.Pattern = Pattern;
		}

		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteString("type", "string");
			if (Format != null)
			{
				Writer.WriteString("format", Format.Value.ToString().ToLowerInvariant());
			}
			if (Pattern != null)
			{
				Writer.WriteString("pattern", Pattern);
			}
		}
	}

	/// <summary>
	/// Represents an enum in a Json schema
	/// </summary>
	public class JsonSchemaEnum : JsonSchemaType
	{
		/// <summary>
		/// Values for the enum
		/// </summary>
		public List<string> Values { get; } = new List<string>();

		/// <summary>
		/// Constructor
		/// </summary>
		public JsonSchemaEnum(IEnumerable<string> Values)
		{
			this.Values.AddRange(Values);
		}

		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteStartArray("enum");
			foreach (string Value in Values)
			{
				Writer.WriteStringValue(Value);
			}
			Writer.WriteEndArray();
		}
	}

	/// <summary>
	/// Represents an array in a Json schema
	/// </summary>
	public class JsonSchemaArray : JsonSchemaType
	{
		/// <summary>
		/// The type of each element
		/// </summary>
		public JsonSchemaType ItemType { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public JsonSchemaArray(JsonSchemaType ItemType)
		{
			this.ItemType = ItemType;
		}

		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteString("type", "array");
			Writer.WriteStartObject("items");
			Writer.WriteType(ItemType);
			Writer.WriteEndObject();
		}
	}

	/// <summary>
	/// Property within a <see cref="JsonSchemaObject"/>
	/// </summary>
	public class JsonSchemaProperty
	{
		/// <summary>
		/// Name of this property
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Description of the property
		/// </summary>
		public string? Description { get; set; }

		/// <summary>
		/// The property type
		/// </summary>
		public JsonSchemaType Type { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public JsonSchemaProperty(string Name, string? Description, JsonSchemaType Type)
		{
			this.Name = Name;
			this.Description = Description;
			this.Type = Type;
		}

		/// <summary>
		/// Writes a property to a schema
		/// </summary>
		/// <param name="Writer"></param>
		public void Write(IJsonSchemaWriter Writer)
		{
			string CamelCaseName = Char.ToLowerInvariant(Name[0]) + Name.Substring(1);
			Writer.WriteStartObject(CamelCaseName);
			Writer.WriteType(Type);
			if (Description != null)
			{
				Writer.WriteString("description", Description);
			}
			Writer.WriteEndObject();
		}
	}

	/// <summary>
	/// Represents an object in a Json schema
	/// </summary>
	public class JsonSchemaObject : JsonSchemaType
	{
		/// <summary>
		/// Properties to allow in this object
		/// </summary>
		public List<JsonSchemaProperty> Properties { get; set; } = new List<JsonSchemaProperty>();

		/// <summary>
		/// Type for additional properties
		/// </summary>
		public JsonSchemaType? AdditionalProperties { get; set; }

		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteString("type", "object");
			if (Properties.Count > 0)
			{
				Writer.WriteStartObject("properties");
				foreach (JsonSchemaProperty Property in Properties)
				{
					Property.Write(Writer);
				}
				Writer.WriteEndObject();
			}

			if (AdditionalProperties == null)
			{
				Writer.WriteBoolean("additionalProperties", false);
			}
			else
			{
				Writer.WriteStartObject("additionalProperties");
				AdditionalProperties.Write(Writer);
				Writer.WriteEndObject();
			}
		}
	}

	/// <summary>
	/// Represents one of a set of object types in a Json schema
	/// </summary>
	public class JsonSchemaOneOf : JsonSchemaType
	{
		/// <summary>
		/// List of valid types
		/// </summary>
		public List<JsonSchemaType> Types { get; } = new List<JsonSchemaType>();

		/// <inheritdoc/>
		public override void Write(IJsonSchemaWriter Writer)
		{
			Writer.WriteStartArray("oneOf");
			foreach (JsonSchemaType Type in Types)
			{
				Writer.WriteStartObject();
				Type.Write(Writer);
				Writer.WriteEndObject();
			}
			Writer.WriteEndArray();
		}
	}
}
