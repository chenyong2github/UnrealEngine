// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Horde.Build.Acls;
using Horde.Build.Streams;
using Horde.Build.Utilities;

namespace Horde.Build.Projects
{
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Stores configuration for a project
	/// </summary>
	[JsonSchema("https://unrealengine.com/horde/project")]
	[JsonSchemaCatalog("Horde Project", "Horde project configuration file", new[] { "*.project.json", "Projects/*.json" })]
	public class ProjectConfig
	{
		/// <summary>
		/// Name for the new project
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Path to the project logo
		/// </summary>
		public string? Logo { get; set; }

		/// <summary>
		/// Categories to include in this project
		/// </summary>
		public List<ProjectCategoryConfig> Categories { get; set; } = new List<ProjectCategoryConfig>();

		/// <summary>
		/// List of streams
		/// </summary>
		public List<StreamConfigRef> Streams { get; set; } = new List<StreamConfigRef>();

		/// <summary>
		/// Acl entries
		/// </summary>
		public AclConfig? Acl { get; set; }
	}

	/// <summary>
	/// Reference to configuration for a stream
	/// </summary>
	public class StreamConfigRef
	{
		/// <summary>
		/// Unique id for the stream
		/// </summary>
		[Required]
		public StreamId Id { get; set; } = StreamId.Empty;

		/// <summary>
		/// Path to the configuration file
		/// </summary>
		[Required]
		public string Path { get; set; } = null!;
	}

	/// <summary>
	/// Information about a category to display for a stream
	/// </summary>
	public class ProjectCategoryConfig
	{
		/// <summary>
		/// Name of this category
		/// </summary>
		[Required]
		public string Name { get; set; } = String.Empty;

		/// <summary>
		/// Index of the row to display this category on
		/// </summary>
		public int Row { get; set; }

		/// <summary>
		/// Whether to show this category on the nav menu
		/// </summary>
		public bool ShowOnNavMenu { get; set; }

		/// <summary>
		/// Patterns for stream names to include
		/// </summary>
		public List<string> IncludePatterns { get; set; } = new List<string>();

		/// <summary>
		/// Patterns for stream names to exclude
		/// </summary>
		public List<string> ExcludePatterns { get; set; } = new List<string>();
	}
}
