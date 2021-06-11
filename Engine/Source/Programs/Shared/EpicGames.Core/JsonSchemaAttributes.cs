// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Attribute setting the identifier for a schema
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class JsonSchemaAttribute : Attribute
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
		/// File patterns to match
		/// </summary>
		public string[]? FileMatch { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Description"></param>
		/// <param name="FileMatch">File patterns to match</param>
		public JsonSchemaCatalogAttribute(string Name, string Description, string? FileMatch)
			: this(Name, Description, (FileMatch == null) ? (string[]?)null : new[] { FileMatch })
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Description"></param>
		/// <param name="FileMatch">File patterns to match</param>
		public JsonSchemaCatalogAttribute(string Name, string Description, string[]? FileMatch)
		{
			this.Name = Name;
			this.Description = Description;
			this.FileMatch = FileMatch;
		}
	}

	/// <summary>
	/// Attribute setting properties for a type to be serialized as a string
	/// </summary>
	public abstract class JsonSchemaTypeAttribute : Attribute
	{
	}

	/// <summary>
	/// Attribute setting properties for a type to be serialized as a string
	/// </summary>
	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct | AttributeTargets.Property)]
	public class JsonSchemaStringAttribute : JsonSchemaTypeAttribute
	{
		/// <summary>
		/// Format of the string
		/// </summary>
		public JsonSchemaStringFormat? Format { get; set; }
	}
}
