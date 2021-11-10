// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using System.Linq;
using System.Security.Cryptography;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Base class for singleton documents
	/// </summary>
	public abstract class SingletonBase
	{
		/// <summary>
		/// Unique id for this singleton
		/// </summary>
		[BsonId]
		public ObjectId Id { get; set; }

		/// <summary>
		/// The revision index of this document
		/// </summary>
		public int Revision { get; set; }

		/// <summary>
		/// Callback to allow the singleton to fix up itself after being read
		/// </summary>
		public virtual void PostLoad()
		{
		}
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
		/// <param name="Now">The current time</param>
		/// <returns>Start and finish time</returns>
		public (DateTimeOffset StartTime, DateTimeOffset FinishTime) GetNext(DateTimeOffset Now)
		{
			TimeSpan Offset = TimeSpan.Zero;
			if (Frequency == ScheduledDowntimeFrequency.Daily)
			{
				double Days = (Now - StartTime).TotalDays;
				if (Days >= 1.0)
				{
					Days -= Days % 1.0;
				}
				Offset = TimeSpan.FromDays(Days);
			}
			else if (Frequency == ScheduledDowntimeFrequency.Weekly)
			{
				double Days = (Now - StartTime).TotalDays;
				if (Days >= 7.0)
				{
					Days -= Days % 7.0;
				}
				Offset = TimeSpan.FromDays(Days);
			}
			return (StartTime + Offset, FinishTime + Offset);
		}

		/// <summary>
		/// Determines if this schedule is active
		/// </summary>
		/// <param name="Now">The current time</param>
		/// <returns>True if downtime is active</returns>
		public bool IsActive(DateTimeOffset Now)
		{
			if (Frequency == ScheduledDowntimeFrequency.Once)
			{
				return Now >= StartTime && Now < FinishTime;
			}
			else if (Frequency == ScheduledDowntimeFrequency.Daily)
			{
				double Days = (Now - StartTime).TotalDays;
				if (Days < 0.0)
				{
					return false;
				}
				else
				{
					return (Days % 1.0) < (FinishTime - StartTime).TotalDays;
				}
			}
			else if (Frequency == ScheduledDowntimeFrequency.Weekly)
			{
				double Days = (Now - StartTime).TotalDays;
				if(Days < 0.0)
				{
					return false;
				}
				else
				{
					return (Days % 7.0) < (FinishTime - StartTime).TotalDays;
				}
			}
			else
			{
				return false;
			}
		}
	}

	/// <summary>
	/// User notice
	/// </summary>
	public class Notice
	{
		/// <summary>
		/// Unique id for this notice
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// Start time to display this message
		/// </summary>
		public DateTime? StartTime { get; set; }

		/// <summary>
		/// Finish time to display this message
		/// </summary>
		public DateTime? FinishTime { get; set; }

		/// <summary>
		/// Message to display
		/// </summary>
		public string Message { get; set; } = String.Empty;
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

	/// <summary>
	/// Global server settings
	/// </summary>
	[SingletonDocument("5e3981cb28b8ec59cd07184a")]
	public class Globals : SingletonBase
	{
		/// <summary>
		/// The unique id for all globals objects
		/// </summary>
		public static ObjectId StaticId { get; } = new ObjectId("5e3981cb28b8ec59cd07184a");

		/// <summary>
		/// Unique instance id of this database
		/// </summary>
		public ObjectId InstanceId { get; set; }

		/// <summary>
		/// Set to true to drop the database on restart.
		/// </summary>
		public bool ForceReset { get; set; }

		/// <summary>
		/// The config revision
		/// </summary>
		public string? ConfigRevision { get; set; }

		/// <summary>
		/// Manually added status messages
		/// </summary>
		public List<Notice> Notices { get; set; } = new List<Notice>();

		/// <summary>
		/// List of Perforce clusters
		/// </summary>
		public List<PerforceCluster> PerforceClusters { get; set; } = new List<PerforceCluster>();

		/// <summary>
		/// Maximum number of simultaneous conforms
		/// </summary>
		public int MaxConformCount { get; set; }

		/// <summary>
		/// Randomly generated JWT signing key
		/// </summary>
		public byte[]? JwtSigningKey { get; set; }

		/// <summary>
		/// The current schema version
		/// </summary>
		public int? SchemaVersion { get; set; }

		/// <summary>
		/// The scheduled downtime
		/// </summary>
		public List<ScheduledDowntime> ScheduledDowntime { get; set; } = new List<ScheduledDowntime>();

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		public Globals()
		{
			InstanceId = ObjectId.GenerateNewId();
		}

		/// <summary>
		/// Rotate the signing key
		/// </summary>
		public void RotateSigningKey()
		{
			JwtSigningKey = RandomNumberGenerator.GetBytes(128);
		}

		/// <summary>
		/// Finds a perforce cluster with the given name
		/// </summary>
		/// <param name="Name">Name of the cluster</param>
		/// <returns></returns>
		public PerforceCluster? FindPerforceCluster(string? Name)
		{
			List<PerforceCluster> Clusters = PerforceClusters;
			if (Clusters.Count == 0)
			{
				Clusters = DefaultClusters;
			}

			if (Name == null)
			{
				return Clusters.FirstOrDefault();
			}
			else
			{
				return Clusters.FirstOrDefault(x => String.Equals(x.Name, Name, StringComparison.OrdinalIgnoreCase));
			}
		}

		static List<PerforceCluster> DefaultClusters { get; } = GetDefaultClusters();

		static List<PerforceCluster> GetDefaultClusters()
		{
			PerforceServer Server = new PerforceServer();
			Server.ServerAndPort = Perforce.P4.P4Server.Get("P4PORT") ?? "perforce:1666";

			PerforceCluster Cluster = new PerforceCluster();
			Cluster.Name = "Default";
			Cluster.CanImpersonate = false;
			Cluster.Servers.Add(Server);

			return new List<PerforceCluster> { Cluster };
		}
	}
}
