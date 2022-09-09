// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Perforce;
using EpicGames.Serialization;
using Horde.Build.Acls;
using Horde.Build.Projects;
using Horde.Build.Tools;
using Horde.Build.Utilities;

namespace Horde.Build.Server
{
	using ProjectId = StringId<IProject>;
	
	/// <summary>
	/// Global configuration
	/// </summary>
	[JsonSchema("https://unrealengine.com/horde/global")]
	[JsonSchemaCatalog("Horde Globals", "Horde global configuration file", "globals.json")]
	public class GlobalConfig
	{
		/// <summary>
		/// List of projects
		/// </summary>
		public List<ProjectConfigRef> Projects { get; set; } = new List<ProjectConfigRef>();

		/// <summary>
		/// List of scheduled downtime
		/// </summary>
		public List<ScheduledDowntime> Downtime { get; set; } = new List<ScheduledDowntime>();

		/// <summary>
		/// List of Perforce clusters
		/// </summary>
		public List<PerforceCluster> PerforceClusters { get; set; } = new List<PerforceCluster>();

		/// <summary>
		/// List of costs of a particular agent type
		/// </summary>
		public List<AgentRateConfig> Rates { get; set; } = new List<AgentRateConfig>();

		/// <summary>
		/// List of compute profiles
		/// </summary>
		public List<ComputeClusterConfig> Compute { get; set; } = new List<ComputeClusterConfig>();

		/// <summary>
		/// Device configuration
		/// </summary>
		public DeviceConfig? Devices { get; set; }

		/// <summary>
		/// List of tools hosted by the server
		/// </summary>
		public List<ToolOptions> Tools { get; set; } = new List<ToolOptions>();

		/// <summary>
		/// Maximum number of conforms to run at once
		/// </summary>
		public int MaxConformCount { get; set; }

		/// <summary>
		/// List of storage namespaces
		/// </summary>
		public StorageConfig? Storage { get; set; }

		/// <summary>
		/// Access control list
		/// </summary>
		public AclConfig Acl { get; set; } = new AclConfig();

		/// <summary>
		/// Finds a perforce cluster with the given name or that contains the provided server
		/// </summary>
		/// <param name="name">Name of the cluster</param>
		/// <param name="serverAndPort">Find cluster which contains server</param>
		/// <returns></returns>
		public PerforceCluster? FindPerforceCluster(string? name, string? serverAndPort = null)
		{
			List<PerforceCluster> clusters = PerforceClusters;

			if (serverAndPort != null)
			{
				return clusters.FirstOrDefault(x => x.Servers.FirstOrDefault(server => String.Equals(server.ServerAndPort, serverAndPort, StringComparison.OrdinalIgnoreCase)) != null);
			}

			if (clusters.Count == 0)
			{
				clusters = DefaultClusters;
			}

			if (name == null)
			{
				return clusters.FirstOrDefault();
			}
			else
			{
				return clusters.FirstOrDefault(x => String.Equals(x.Name, name, StringComparison.OrdinalIgnoreCase));
			}
		}

		static List<PerforceCluster> DefaultClusters { get; } = GetDefaultClusters();

		static List<PerforceCluster> GetDefaultClusters()
		{
			PerforceServer server = new PerforceServer();
			server.ServerAndPort = PerforceSettings.Default.ServerAndPort;

			PerforceCluster cluster = new PerforceCluster();
			cluster.Name = "Default";
			cluster.CanImpersonate = false;
			cluster.Servers.Add(server);

			return new List<PerforceCluster> { cluster };
		}
	}

	/// <summary>
	/// Profile for executing compute requests
	/// </summary>
	public class ComputeClusterConfig
	{
		/// <summary>
		/// Name of the partition
		/// </summary>
		public string Id { get; set; } = "default";

		/// <summary>
		/// Name of the namespace to use
		/// </summary>
		public string NamespaceId { get; set; } = "horde.compute";

		/// <summary>
		/// Name of the input bucket
		/// </summary>
		public string RequestBucketId { get; set; } = "requests";

		/// <summary>
		/// Name of the output bucket
		/// </summary>
		public string ResponseBucketId { get; set; } = "responses";

		/// <summary>
		/// Access control list
		/// </summary>
		public AclConfig? Acl { get; set; }
	}

	/// <summary>
	/// References a project configuration
	/// </summary>
	public class ProjectConfigRef
	{
		/// <summary>
		/// Unique id for the project
		/// </summary>
		[Required]
		public ProjectId Id { get; set; } = ProjectId.Empty;

		/// <summary>
		/// Config path for the project
		/// </summary>
		[Required]
		public string Path { get; set; } = null!;
	}

	/// <summary>
	/// Configuration for storage system
	/// </summary>
	public class StorageConfig
	{
		/// <summary>
		/// List of storage namespaces
		/// </summary>
		public List<NamespaceConfig> Namespaces { get; set; } = new List<NamespaceConfig>();
	}

	/// <summary>
	/// Configuration for a storage namespace
	/// </summary>
	public class NamespaceConfig
	{
		/// <summary>
		/// Identifier for this namespace
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// Buckets within this namespace
		/// </summary>
		public List<BucketConfig> Buckets { get; set; } = new List<BucketConfig>();

		/// <summary>
		/// Access control for this namespace
		/// </summary>
		public AclConfig? Acl { get; set; }
	}

	/// <summary>
	/// Configuration for a device platform 
	/// </summary>
	public class DevicePlatformConfig
	{
		/// <summary>
		/// The id for this platform 
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// List of platform names for this device, which may be requested by Gauntlet 
		/// </summary>
		public List<string> Names { get; set; } = new List<string>();

		/// <summary>
		/// Model name for the high perf spec, which may be requested by Gauntlet (Deprecated)
		/// </summary>
		public string? LegacyPerfSpecHighModel { get; set; }
	}

	/// <summary>
	/// Configuration for devices
	/// </summary>
	public class DeviceConfig
	{
		/// <summary>
		/// List of device platforms
		/// </summary>
		public List<DevicePlatformConfig> Platforms { get; set; } = new List<DevicePlatformConfig>();
	}


	/// <summary>
	/// Configuration for a bucket
	/// </summary>
	public class BucketConfig
	{
		/// <summary>
		/// Identifier for the bucket
		/// </summary>
		public string Id { get; set; } = String.Empty;
	}

	/// <summary>
	/// Describes the monetary cost of agents matching a particular criteris
	/// </summary>
	public class AgentRateConfig
	{
		/// <summary>
		/// Condition string
		/// </summary>
		[CbField("c")]
		public Condition? Condition { get; set; }

		/// <summary>
		/// Rate for this agent
		/// </summary>
		[CbField("r")]
		public double Rate { get; set; }
	}

	/// <summary>
	/// How frequently the maintence window repeats
	/// </summary>
	public enum ScheduledDowntimeFrequency
	{
		/// <summary>
		/// Once
		/// </summary>
		Once,

		/// <summary>
		/// Every day
		/// </summary>
		Daily,

		/// <summary>
		/// Every week
		/// </summary>
		Weekly,
	}

	/// <summary>
	/// Settings for the maintenance window
	/// </summary>
	public class ScheduledDowntime
	{
		/// <summary>
		/// Start time
		/// </summary>
		public DateTimeOffset StartTime { get; set; }

		/// <summary>
		/// Finish time
		/// </summary>
		public DateTimeOffset FinishTime { get; set; }

		/// <summary>
		/// Frequency that the window repeats
		/// </summary>
		public ScheduledDowntimeFrequency Frequency { get; set; } = ScheduledDowntimeFrequency.Once;

		/// <summary>
		/// Gets the next scheduled downtime
		/// </summary>
		/// <param name="now">The current time</param>
		/// <returns>Start and finish time</returns>
		public (DateTimeOffset StartTime, DateTimeOffset FinishTime) GetNext(DateTimeOffset now)
		{
			TimeSpan offset = TimeSpan.Zero;
			if (Frequency == ScheduledDowntimeFrequency.Daily)
			{
				double days = (now - StartTime).TotalDays;
				if (days >= 1.0)
				{
					days -= days % 1.0;
				}
				offset = TimeSpan.FromDays(days);
			}
			else if (Frequency == ScheduledDowntimeFrequency.Weekly)
			{
				double days = (now - StartTime).TotalDays;
				if (days >= 7.0)
				{
					days -= days % 7.0;
				}
				offset = TimeSpan.FromDays(days);
			}
			return (StartTime + offset, FinishTime + offset);
		}

		/// <summary>
		/// Determines if this schedule is active
		/// </summary>
		/// <param name="now">The current time</param>
		/// <returns>True if downtime is active</returns>
		public bool IsActive(DateTimeOffset now)
		{
			if (Frequency == ScheduledDowntimeFrequency.Once)
			{
				return now >= StartTime && now < FinishTime;
			}
			else if (Frequency == ScheduledDowntimeFrequency.Daily)
			{
				double days = (now - StartTime).TotalDays;
				if (days < 0.0)
				{
					return false;
				}
				else
				{
					return (days % 1.0) < (FinishTime - StartTime).TotalDays;
				}
			}
			else if (Frequency == ScheduledDowntimeFrequency.Weekly)
			{
				double days = (now - StartTime).TotalDays;
				if (days < 0.0)
				{
					return false;
				}
				else
				{
					return (days % 7.0) < (FinishTime - StartTime).TotalDays;
				}
			}
			else
			{
				return false;
			}
		}
	}

	/// <summary>
	/// Path to a platform and stream to use for syncing AutoSDK
	/// </summary>
	public class AutoSdkWorkspace
	{
		/// <summary>
		/// Name of this workspace
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// The agent properties to check (eg. "OSFamily=Windows")
		/// </summary>
		public List<string> Properties { get; set; } = new List<string>();

		/// <summary>
		/// Username for logging in to the server
		/// </summary>
		public string? UserName { get; set; }

		/// <summary>
		/// Stream to use
		/// </summary>
		[Required]
		public string? Stream { get; set; }
	}

	/// <summary>
	/// Information about an individual Perforce server
	/// </summary>
	public class PerforceServer
	{
		/// <summary>
		/// The server and port. The server may be a DNS entry with multiple records, in which case it will be actively load balanced.
		/// </summary>
		public string ServerAndPort { get; set; } = "perforce:1666";

		/// <summary>
		/// Whether to query the healthcheck address under each server
		/// </summary>
		public bool HealthCheck { get; set; }

		/// <summary>
		/// Whether to resolve the DNS entries and load balance between different hosts
		/// </summary>
		public bool ResolveDns { get; set; }

		/// <summary>
		/// Maximum number of simultaneous conforms on this server
		/// </summary>
		public int MaxConformCount { get; set; }

		/// <summary>
		/// Optional condition for a machine to be eligable to use this server
		/// </summary>
		public Condition? Condition { get; set; }

		/// <summary>
		/// List of properties for an agent to be eligable to use this server
		/// </summary>
		public List<string>? Properties { get; set; }
	}

	/// <summary>
	/// Credentials for a Perforce user
	/// </summary>
	public class PerforceCredentials
	{
		/// <summary>
		/// The username
		/// </summary>
		public string UserName { get; set; } = String.Empty;

		/// <summary>
		/// Password for the user
		/// </summary>
		public string Password { get; set; } = String.Empty;
	}

	/// <summary>
	/// Information about a cluster of Perforce servers. 
	/// </summary>
	public class PerforceCluster
	{
		/// <summary>
		/// The default cluster name
		/// </summary>
		public const string DefaultName = "Default";

		/// <summary>
		/// Name of the cluster
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Username for Horde to log in to this server. Will use the default user if not set.
		/// </summary>
		public string? ServiceAccount { get; set; }

		/// <summary>
		/// Whether the service account can impersonate other users
		/// </summary>
		public bool CanImpersonate { get; set; } = true;

		/// <summary>
		/// List of servers
		/// </summary>
		public List<PerforceServer> Servers { get; set; } = new List<PerforceServer>();

		/// <summary>
		/// List of server credentials
		/// </summary>
		public List<PerforceCredentials> Credentials { get; set; } = new List<PerforceCredentials>();

		/// <summary>
		/// List of autosdk streams
		/// </summary>
		public List<AutoSdkWorkspace> AutoSdk { get; set; } = new List<AutoSdkWorkspace>();
	}
}

