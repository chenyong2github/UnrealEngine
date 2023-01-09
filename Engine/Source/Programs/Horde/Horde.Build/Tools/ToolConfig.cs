// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using Horde.Build.Acls;
using Horde.Build.Utilities;

namespace Horde.Build.Tools
{
	using ToolId = StringId<ITool>;

	/// <summary>
	/// Options for configuring a tool
	/// </summary>
	[DebuggerDisplay("{Id}")]
	public class ToolConfig
	{
		/// <inheritdoc cref="VersionedDocument{ToolId, Tool}.Id"/>
		[Required]
		public ToolId Id { get; set; }

		/// <inheritdoc cref="ITool.Name"/>
		[Required]
		public string Name { get; set; }

		/// <inheritdoc cref="ITool.Description"/>
		public string Description { get; set; }

		/// <inheritdoc cref="ITool.Public"/>
		public bool Public { get; set; }

		/// <inheritdoc cref="ITool.Acl"/>
		public AclV2? Acl { get; set; }

		/// <summary>
		/// Default constructor for serialization
		/// </summary>
		public ToolConfig()
		{
			Name = String.Empty;
			Description = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public ToolConfig(ToolId id)
		{
			Id = id;
			Name = id.ToString();
			Description = String.Empty;
		}
	}

	/// <summary>
	/// Options for a new deployment
	/// </summary>
	public class ToolDeploymentConfig
	{
		/// <summary>
		/// Default mime types for deployment data
		/// </summary>
		public const string DefaultMimeType = "application/zip";

		/// <inheritdoc cref="IToolDeployment.Version"/>
		public string Version { get; set; } = "Unknown";

		/// <inheritdoc cref="IToolDeployment.Duration"/>
		public TimeSpan Duration { get; set; }

		/// <inheritdoc cref="IToolDeployment.MimeType"/>
		public string MimeType { get; set; } = DefaultMimeType;

		/// <summary>
		/// Whether to create the deployment in a paused state
		/// </summary>
		public bool CreatePaused { get; set; }
	}
}
