// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using EpicGames.Horde.Storage;
using Horde.Build.Acls;
using Horde.Build.Storage.Backends;
using Horde.Build.Utilities;

namespace Horde.Build.Storage
{
	using BackendId = StringId<IStorageBackend>;

	/// <summary>
	/// Well-known namespace identifiers
	/// </summary>
	public static class Namespace
	{
		/// <summary>
		/// Storage of artifacts 
		/// </summary>
		public static NamespaceId Artifacts { get; } = new NamespaceId("horde-artifacts");

		/// <summary>
		/// Replicated Perforce data
		/// </summary>
		public static NamespaceId Perforce { get; } = new NamespaceId("horde-perforce");

		/// <summary>
		/// Log data
		/// </summary>
		public static NamespaceId Logs { get; } = new NamespaceId("horde-logs");

		/// <summary>
		/// Storage of tool data
		/// </summary>
		public static NamespaceId Tools { get; } = new NamespaceId("horde-tools");
	}

	/// <summary>
	/// Configuration for storage
	/// </summary>
	public class StorageConfig
	{
		/// <summary>
		/// List of storage backends
		/// </summary>
		public List<BackendConfig> Backends { get; set; } = new List<BackendConfig>();

		/// <summary>
		/// List of namespaces for storage
		/// </summary>
		public List<NamespaceConfig> Namespaces { get; set; } = new List<NamespaceConfig>();
	}

	/// <summary>
	/// Common settings object for different providers
	/// </summary>
	public class BackendConfig : IStorageBackendOptions
	{
		/// <summary>
		/// The storage backend ID
		/// </summary>
		[Required]
		public BackendId Id { get; set; }

		/// <summary>
		/// Base backend to copy default settings from
		/// </summary>
		public BackendId Base { get; set; }

		/// <inheritdoc/>
		public StorageBackendType? Type { get; set; }

		/// <inheritdoc/>
		public string? BaseDir { get; set; }

		/// <inheritdoc/>
		public string? AwsBucketName { get; set; }

		/// <inheritdoc/>
		public string? AwsBucketPath { get; set; }

		/// <inheritdoc/>
		public AwsCredentialsType AwsCredentials { get; set; }

		/// <inheritdoc/>
		public string? AwsRole { get; set; }

		/// <inheritdoc/>
		public string? AwsProfile { get; set; }

		/// <inheritdoc/>
		public string? AwsRegion { get; set; }

		/// <inheritdoc/>
		public string? RelayServer { get; set; }

		/// <inheritdoc/>
		public string? RelayToken { get; set; }
	}

	/// <summary>
	/// Configuration of a particular namespace
	/// </summary>
	public class NamespaceConfig
	{
		/// <summary>
		/// Identifier for this namespace
		/// </summary>
		[Required]
		public NamespaceId Id { get; set; }

		/// <summary>
		/// Backend to use for this namespace
		/// </summary>
		[Required]
		public BackendId Backend { get; set; }

		/// <summary>
		/// Prefix for items within this namespace
		/// </summary>
		public string Prefix { get; set; } = String.Empty;

		/// <summary>
		/// How frequently to run garbage collection, in hours.
		/// </summary>
		public double GcFrequencyHrs { get; set; } = 2.0;

		/// <summary>
		/// How long to keep newly uploaded orphanned objects before allowing them to be deleted, in hours.
		/// </summary>
		public double GcDelayHrs { get; set; } = 6.0;

		/// <summary>
		/// Support querying exports by their aliases
		/// </summary>
		public bool EnableAliases { get; set; }

		/// <summary>
		/// Access list for this namespace
		/// </summary>
		public AclConfig Acl { get; set; } = new AclConfig();
	}
}
