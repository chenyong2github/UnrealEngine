// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using Serilog;
using System.Diagnostics;
using System.Threading.Tasks;
using HordeServer.Collections;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Common;

namespace HordeServer.Models
{
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;
	using AgentSoftwareVersion = StringId<IAgentSoftwareCollection>;
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;

	/// <summary>
	/// Information about a workspace synced to an agent
	/// </summary>
	public class AgentWorkspace
	{
		/// <summary>
		/// Name of the Perforce cluster to use
		/// </summary>
		public string? Cluster { get; set; }

		/// <summary>
		/// User to log into Perforce with (eg. buildmachine)
		/// </summary>
		public string? UserName { get; set; }

		/// <summary>
		/// Identifier to distinguish this workspace from other workspaces
		/// </summary>
		public string Identifier { get; set; }

		/// <summary>
		/// The stream to sync
		/// </summary>
		public string Stream { get; set; }

		/// <summary>
		/// Custom view for the workspace
		/// </summary>
		public List<string>? View { get; set; }

		/// <summary>
		/// Whether to use an incremental workspace
		/// </summary>
		public bool bIncremental { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Cluster">Name of the Perforce cluster</param>
		/// <param name="UserName">User to log into Perforce with (eg. buildmachine)</param>
		/// <param name="Identifier">Identifier to distinguish this workspace from other workspaces</param>
		/// <param name="Stream">The stream to sync</param>
		/// <param name="View">Custom view for the workspace</param>
		/// <param name="bIncremental">Whether to use an incremental workspace</param>
		public AgentWorkspace(string? Cluster, string? UserName, string Identifier, string Stream, List<string>? View, bool bIncremental)
		{
			if (!String.IsNullOrEmpty(Cluster))
			{
				this.Cluster = Cluster;
			}
			if (!String.IsNullOrEmpty(UserName))
			{
				this.UserName = UserName;
			}
			this.Identifier = Identifier;
			this.Stream = Stream;
			this.View = View;
			this.bIncremental = bIncremental;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Workspace">RPC message to construct from</param>
		public AgentWorkspace(HordeCommon.Rpc.Messages.AgentWorkspace Workspace)
			: this(Workspace.ConfiguredCluster, Workspace.ConfiguredUserName, Workspace.Identifier, Workspace.Stream, (Workspace.View.Count > 0) ? Workspace.View.ToList() : null, Workspace.Incremental)
		{
		}

		/// <summary>
		/// Gets a digest of the settings for this workspace
		/// </summary>
		/// <returns>Digest for the workspace settings</returns>
		public string GetDigest()
		{
#pragma warning disable CA5351 // Do Not Use Broken Cryptographic Algorithms
			using (MD5 Hasher = MD5.Create())
			{
				byte[] Data = BsonExtensionMethods.ToBson(this);
				return BitConverter.ToString(Hasher.ComputeHash(Data)).Replace("-", "", StringComparison.Ordinal);
			}
#pragma warning restore CA5351
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj)
		{
			AgentWorkspace? Other = Obj as AgentWorkspace;
			if (Other == null)
			{
				return false;
			}
			if (Cluster != Other.Cluster || UserName != Other.UserName || Identifier != Other.Identifier || Stream != Other.Stream || bIncremental != Other.bIncremental)
			{
				return false;
			}
			if (!Enumerable.SequenceEqual(View ?? new List<string>(), Other.View ?? new List<string>()))
			{
				return false;
			}
			return true;
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			return HashCode.Combine(Cluster, UserName, Identifier, Stream, bIncremental); // Ignore 'View' for now
		}

		/// <summary>
		/// Checks if two workspace sets are equivalent, ignoring order
		/// </summary>
		/// <param name="WorkspacesA">First list of workspaces</param>
		/// <param name="WorkspacesB">Second list of workspaces</param>
		/// <returns>True if the sets are equivalent</returns>
		public static bool SetEquals(IReadOnlyList<AgentWorkspace> WorkspacesA, IReadOnlyList<AgentWorkspace> WorkspacesB)
		{
			HashSet<AgentWorkspace> WorkspacesSetA = new HashSet<AgentWorkspace>(WorkspacesA);
			return WorkspacesSetA.SetEquals(WorkspacesB);
		}

		/// <summary>
		/// Converts this workspace to an RPC message
		/// </summary>
		/// <param name="Server">The Perforce server</param>
		/// <param name="Credentials">Credentials for the server</param>
		/// <returns>The RPC message</returns>
		public HordeCommon.Rpc.Messages.AgentWorkspace ToRpcMessage(IPerforceServer Server, PerforceCredentials? Credentials)
		{
			// Construct the message
			HordeCommon.Rpc.Messages.AgentWorkspace Result = new HordeCommon.Rpc.Messages.AgentWorkspace();
			Result.ConfiguredCluster = Cluster;
			Result.ConfiguredUserName = UserName;
			Result.ServerAndPort = Server.ServerAndPort;
			Result.UserName = Credentials?.UserName ?? UserName;
			Result.Password = Credentials?.Password;
			Result.Identifier = Identifier;
			Result.Stream = Stream;
			if (View != null)
			{
				Result.View.AddRange(View);
			}
			Result.Incremental = bIncremental;
			return Result;
		}
	}

	/// <summary>
	/// Document describing an active lease
	/// </summary>
	public class AgentLease
	{
		/// <summary>
		/// Lease returned when a task source wants to run, but needs to wait for other tasks to finish first.
		/// </summary>
		public static AgentLease Drain { get; } = new AgentLease(LeaseId.Empty, String.Empty, null, null, null, LeaseState.Pending, null, true, null);

		/// <summary>
		/// Name of this lease
		/// </summary>
		[BsonRequired]
		public LeaseId Id { get; set; }

		/// <summary>
		/// Name of this lease
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The current state of the lease
		/// </summary>
		public LeaseState State { get; set; }

		/// <summary>
		/// The stream for this lease
		/// </summary>
		public StreamId? StreamId { get; set; }

		/// <summary>
		/// The pool for this lease
		/// </summary>
		public PoolId? PoolId { get; set; }

		/// <summary>
		/// Optional log for this lease
		/// </summary>
		public LogId? LogId { get; set; }

		/// <summary>
		/// Time at which the lease started
		/// </summary>
		[BsonRequired]
		public DateTime StartTime { get; set; }

		/// <summary>
		/// Time at which the lease should be terminated
		/// </summary>
		public DateTime? ExpiryTime { get; set; }

		/// <summary>
		/// Flag indicating whether this lease has been accepted by the agent
		/// </summary>
		public bool Active { get; set; }

		/// <summary>
		/// Resources used by this lease
		/// </summary>
		public IReadOnlyDictionary<string, int>? Resources { get; set; }

		/// <summary>
		/// Whether the lease requires exclusive access to the agent
		/// </summary>
		public bool Exclusive { get; set; }

		/// <summary>
		/// For leases in the pending state, encodes an "any" protobuf containing the payload for the agent to execute the lease.
		/// </summary>
		public byte[]? Payload { get; set; }

		/// <summary>
		/// Private constructor
		/// </summary>
		[BsonConstructor]
		private AgentLease()
		{
			Name = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Identifier for the lease</param>
		/// <param name="Name">Name of this lease</param>
		/// <param name="StreamId"></param>
		/// <param name="PoolId"></param>
		/// <param name="LogId">Unique id for the log</param>
		/// <param name="State">State for the lease</param>
		/// <param name="Resources">Resources required for this lease</param>
		/// <param name="Exclusive">Whether to reserve the entire device</param>
		/// <param name="Payload">Encoded "any" protobuf describing the contents of the payload</param>
		public AgentLease(LeaseId Id, string Name, StreamId? StreamId, PoolId? PoolId, LogId? LogId, LeaseState State, IReadOnlyDictionary<string, int>? Resources, bool Exclusive, byte[]? Payload)
		{
			this.Id = Id;
			this.Name = Name;
			this.StreamId = StreamId;
			this.PoolId = PoolId;
			this.LogId = LogId;
			this.State = State;
			this.Resources = Resources;
			this.Exclusive = Exclusive;
			this.Payload = Payload;
			StartTime = DateTime.UtcNow;
		}

		/// <summary>
		/// Determines if this is a conform lease
		/// </summary>
		/// <returns>True if this is a conform lease</returns>
		public bool IsConformLease()
		{
			if (Payload != null)
			{
				Any BasePayload = Any.Parser.ParseFrom(Payload);
				if (BasePayload.Is(ConformTask.Descriptor))
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Gets user-readable payload information
		/// </summary>
		/// <param name="Payload">The payload data</param>
		/// <returns>Dictionary of key/value pairs for the payload</returns>
		public static Dictionary<string, string>? GetPayloadDetails(ReadOnlyMemory<byte>? Payload)
		{
			Dictionary<string, string>? Details = null;
			if (Payload != null)
			{
				Any BasePayload = Any.Parser.ParseFrom(Payload.Value.ToArray());

				Details = new Dictionary<string, string>();

				ConformTask ConformTask;
				if(BasePayload.TryUnpack(out ConformTask))
				{
					Details["Type"] = "Conform";
					Details["LogId"] = ConformTask.LogId;
				}

				ExecuteJobTask JobTask;
				if (BasePayload.TryUnpack(out JobTask))
				{
					Details["Type"] = "Job";
					Details["JobId"] = JobTask.JobId;
					Details["BatchId"] = JobTask.BatchId;
					Details["LogId"] = JobTask.LogId;
				}

				UpgradeTask UpgradeTask;
				if (BasePayload.TryUnpack(out UpgradeTask))
				{
					Details["Type"] = "Upgrade";
					Details["SoftwareId"] = UpgradeTask.SoftwareId;
					Details["LogId"] = UpgradeTask.LogId;
				}
			}
			return Details;
		}

		/// <summary>
		/// Converts this lease to an RPC message
		/// </summary>
		/// <returns>RPC message</returns>
		public HordeCommon.Rpc.Messages.Lease ToRpcMessage()
		{
			HordeCommon.Rpc.Messages.Lease Lease = new HordeCommon.Rpc.Messages.Lease();
			Lease.Id = Id.ToString();
			Lease.Payload = Google.Protobuf.WellKnownTypes.Any.Parser.ParseFrom(Payload); 
			Lease.State = State;
			return Lease;
		}
	}

	/// <summary>
	/// Well-known property names for agents
	/// </summary>
	static class KnownPropertyNames
	{
		/// <summary>
		/// The agent id
		/// </summary>
		public const string Id = "Id";

		/// <summary>
		/// The operating system (Linux, MacOS, Windows)
		/// </summary>
		public const string OSFamily = "OSFamily";

		/// <summary>
		/// Pools that this agent belongs to
		/// </summary>
		public const string Pool = "Pool";

		/// <summary>
		/// Number of logical cores
		/// </summary>
		public const string LogicalCores = "LogicalCores";

		/// <summary>
		/// Amount of RAM, in GB
		/// </summary>
		public const string RAM = "RAM";
	}

	/// <summary>
	/// Mirrors an Agent document in the database
	/// </summary>
	public interface IAgent
	{
		/// <summary>
		/// Randomly generated unique id for this agent.
		/// </summary>
		public AgentId Id { get; }

		/// <summary>
		/// The current session id, if it's online
		/// </summary>
		public ObjectId? SessionId { get; }

		/// <summary>
		/// Time at which the current session expires. 
		/// </summary>
		public DateTime? SessionExpiresAt { get; }

		/// <summary>
		/// Current status of this agent
		/// </summary>
		public AgentStatus Status { get; }

		/// <summary>
		/// Whether the agent is enabled
		/// </summary>
		public bool Enabled { get; }

		/// <summary>
		/// Whether the agent should be included on the dashboard. This is set to true for ephemeral agents once they are no longer online, or agents that are explicitly deleted.
		/// </summary>
		public bool Deleted { get; }

		/// <summary>
		/// Version of the software running on this agent
		/// </summary>
		public string? Version { get; }

		/// <summary>
		/// Arbitrary comment for the agent (useful for disable reasons etc)
		/// </summary>
		public string? Comment { get; }

		/// <summary>
		/// List of properties for this agent
		/// </summary>
		public IReadOnlyList<string> Properties { get; }

		/// <summary>
		/// List of resources available to the agent
		/// </summary>
		public IReadOnlyDictionary<string, int> Resources { get; }

		/// <summary>
		/// Channel for the software running on this agent. Uses <see cref="AgentSoftwareService.DefaultChannelName"/> if not specified
		/// </summary>
		public AgentSoftwareChannelName? Channel { get; }

		/// <summary>
		/// Last upgrade that was attempted
		/// </summary>
		public string? LastUpgradeVersion { get; }

		/// <summary>
		/// Time that which the last upgrade was attempted
		/// </summary>
		public DateTime? LastUpgradeTime { get; }

		/// <summary>
		/// Dynamically applied pools
		/// </summary>
		public IReadOnlyList<PoolId> DynamicPools { get; }

		/// <summary>
		/// List of manually assigned pools for agent
		/// </summary>
		public IReadOnlyList<PoolId> ExplicitPools { get; }

		/// <summary>
		/// Whether a conform is requested
		/// </summary>
		public bool RequestConform { get; }

		/// <summary>
		/// Whether a machine restart is requested
		/// </summary>
		public bool RequestRestart { get; }

		/// <summary>
		/// Whether the machine should be shutdown
		/// </summary>
		public bool RequestShutdown { get; }

		/// <summary>
		/// List of workspaces currently synced to this machine
		/// </summary>
		public IReadOnlyList<AgentWorkspace> Workspaces { get; }

		/// <summary>
		/// Time at which the last conform job ran
		/// </summary>
		public DateTime LastConformTime { get; }

		/// <summary>
		/// Number of times a conform job has failed
		/// </summary>
		public int? ConformAttemptCount { get; }

		/// <summary>
		/// Array of active leases.
		/// </summary>
		public IReadOnlyList<AgentLease> Leases { get; }

		/// <summary>
		/// ACL for modifying this agent
		/// </summary>
		public Acl? Acl { get; }

		/// <summary>
		/// Last time that the agent was modified
		/// </summary>
		public DateTime UpdateTime { get; }

		/// <summary>
		/// Update counter for this document. Any updates should compare-and-swap based on the value of this counter, or increment it in the case of server-side updates.
		/// </summary>
		public uint UpdateIndex { get; }
	}

	/// <summary>
	/// Extension methods for IAgent
	/// </summary>
	public static class AgentExtensions
	{
		/// <summary>
		/// Determines whether this agent is online
		/// </summary>
		/// <returns></returns>
		public static bool IsSessionValid(this IAgent Agent, DateTime UtcNow)
		{
			return Agent.SessionId.HasValue && Agent.SessionExpiresAt.HasValue && UtcNow < Agent.SessionExpiresAt.Value;
		}

		/// <summary>
		/// Tests whether an agent is in the given pool
		/// </summary>
		/// <param name="Agent"></param>
		/// <param name="PoolId"></param>
		/// <returns></returns>
		public static bool IsInPool(this IAgent Agent, PoolId PoolId)
		{
			return Agent.DynamicPools.Contains(PoolId) || Agent.ExplicitPools.Contains(PoolId);
		}

		/// <summary>
		/// Get all the pools for each agent
		/// </summary>
		/// <param name="Agent">The agent to query</param>
		/// <returns></returns>
		public static IEnumerable<PoolId> GetPools(this IAgent Agent)
		{
			foreach (PoolId PoolId in Agent.DynamicPools)
			{
				yield return PoolId;
			}
			foreach (PoolId PoolId in Agent.ExplicitPools)
			{
				yield return PoolId;
			}
		}

		/// <summary>
		/// Tests whether an agent has a particular property
		/// </summary>
		/// <param name="Agent"></param>
		/// <param name="Property"></param>
		/// <returns></returns>
		public static bool HasProperty(this IAgent Agent, string Property)
		{
			return Agent.Properties.BinarySearch(Property, StringComparer.OrdinalIgnoreCase) >= 0;
		}

		/// <summary>
		/// Finds property values from a sorted list of Name=Value pairs
		/// </summary>
		/// <param name="Agent">The agent to query</param>
		/// <param name="Name">Name of the property to find</param>
		/// <returns>Property values</returns>
		public static IEnumerable<string> GetPropertyValues(this IAgent Agent, string Name)
		{
			if (Name.Equals(KnownPropertyNames.Id, StringComparison.OrdinalIgnoreCase))
			{
				yield return Agent.Id.ToString();
			}
			else if (Name.Equals(KnownPropertyNames.Pool, StringComparison.OrdinalIgnoreCase))
			{
				foreach (PoolId PoolId in Agent.GetPools())
				{
					yield return PoolId.ToString();
				}
			}
			else
			{
				int Index = Agent.Properties.BinarySearch(Name, StringComparer.OrdinalIgnoreCase);
				if (Index < 0)
				{
					Index = ~Index;
					while (Index < Agent.Properties.Count)
					{
						string Property = Agent.Properties[Index];
						if (Property.Length <= Name.Length || !Property.StartsWith(Name, StringComparison.OrdinalIgnoreCase) || Property[Name.Length] != '=')
						{
							break;
						}
						yield return Property.Substring(Name.Length + 1);
					}
				}
			}
		}

		/// <summary>
		/// Evaluates a condition against an agent
		/// </summary>
		/// <param name="Agent">The agent to evaluate</param>
		/// <param name="Condition">The condition to evaluate</param>
		/// <returns>True if the agent satisfies the condition</returns>
		public static bool SatisfiesCondition(this IAgent Agent, Condition Condition)
		{
			return Condition.Evaluate(x => Agent.GetPropertyValues(x));
		}

		/// <summary>
		/// Determine whether it's possible to add a lease for the given resources
		/// </summary>
		/// <param name="Agent">The agent to create a lease for</param>
		/// <param name="Requirements">Requirements for the lease</param>
		/// <returns>True if the new lease can be granted</returns>
		public static bool MeetsRequirements(this IAgent Agent, Requirements Requirements)
		{
			return MeetsRequirements(Agent, Requirements.Condition, Requirements.Resources, Requirements.Exclusive);
		}

		/// <summary>
		/// Determine whether it's possible to add a lease for the given resources
		/// </summary>
		/// <param name="Agent">The agent to create a lease for</param>
		/// <param name="Exclusive">Whether t</param>
		/// <param name="Condition">Condition to satisfy</param>
		/// <param name="Resources">Resources required to execute</param>
		/// <returns>True if the new lease can be granted</returns>
		public static bool MeetsRequirements(this IAgent Agent, Condition? Condition, Dictionary<string, int>? Resources, bool Exclusive)
		{
			if (!Agent.Enabled || Agent.Status != AgentStatus.Ok)
			{
				return false;
			}
			if (Agent.Leases.Any(x => x.Exclusive))
			{
				return false;
			}
			if (Exclusive && Agent.Leases.Any())
			{
				return false;
			}
			if (Condition != null && !Agent.SatisfiesCondition(Condition))
			{
				return false;
			}
			if (Resources != null)
			{
				foreach ((string Name, int Count) in Resources)
				{
					int RemainingCount;
					if (!Agent.Resources.TryGetValue(Name, out RemainingCount))
					{
						return false;
					}
					foreach (AgentLease Lease in Agent.Leases)
					{
						if (Lease.Resources != null)
						{
							int LeaseCount;
							Lease.Resources.TryGetValue(Name, out LeaseCount);
							RemainingCount -= LeaseCount;
						}
					}
					if (RemainingCount < Count)
					{
						return false;
					}
				}
			}
			return true;
		}

		/// <summary>
		/// Gets all the autosdk workspaces required for an agent
		/// </summary>
		/// <param name="Agent"></param>
		/// <param name="Globals"></param>
		/// <param name="Workspaces"></param>
		/// <returns></returns>
		public static HashSet<AgentWorkspace> GetAutoSdkWorkspaces(this IAgent Agent, Globals Globals, List<AgentWorkspace> Workspaces)
		{
			HashSet<AgentWorkspace> AutoSdkWorkspaces = new HashSet<AgentWorkspace>();
			foreach (string? ClusterName in Workspaces.Select(x => x.Cluster).Distinct())
			{
				PerforceCluster? Cluster = Globals.FindPerforceCluster(ClusterName);
				if (Cluster != null)
				{
					AgentWorkspace? AutoSdkWorkspace = GetAutoSdkWorkspace(Agent, Cluster);
					if (AutoSdkWorkspace != null)
					{
						AutoSdkWorkspaces.Add(AutoSdkWorkspace);
					}
				}
			}
			return AutoSdkWorkspaces;
		}

		/// <summary>
		/// Get the AutoSDK workspace required for an agent
		/// </summary>
		/// <param name="Agent"></param>
		/// <param name="Cluster">The perforce cluster to get a workspace for</param>
		/// <returns></returns>
		public static AgentWorkspace? GetAutoSdkWorkspace(this IAgent Agent, PerforceCluster Cluster)
		{
			foreach (AutoSdkWorkspace AutoSdk in Cluster.AutoSdk)
			{
				if (AutoSdk.Stream != null && AutoSdk.Properties.All(x => Agent.Properties.Contains(x)))
				{
					return new AgentWorkspace(Cluster.Name, AutoSdk.UserName, AutoSdk.Name ?? "AutoSDK", AutoSdk.Stream!, null, true);
				}
			}
			return null;
		}

		/// <summary>
		/// Converts this workspace to an RPC message
		/// </summary>
		/// <param name="Agent">The agent to get a workspace for</param>
		/// <param name="Workspace">The workspace definition</param>
		/// <param name="Cluster">The global state</param>
		/// <param name="LoadBalancer">The Perforce load balancer</param>
		/// <param name="WorkspaceMessages">List of messages</param>
		/// <returns>The RPC message</returns>
		public static async Task<bool> TryAddWorkspaceMessage(this IAgent Agent, AgentWorkspace Workspace, PerforceCluster Cluster, PerforceLoadBalancer LoadBalancer, IList<HordeCommon.Rpc.Messages.AgentWorkspace> WorkspaceMessages)
		{
			// Find a matching server, trying to use a previously selected one if possible
			string? BaseServerAndPort;
			string? ServerAndPort;

			HordeCommon.Rpc.Messages.AgentWorkspace? ExistingWorkspace = WorkspaceMessages.FirstOrDefault(x => x.ConfiguredCluster == Workspace.Cluster);
			if(ExistingWorkspace != null)
			{
				BaseServerAndPort = ExistingWorkspace.BaseServerAndPort;
				ServerAndPort = ExistingWorkspace.ServerAndPort;
			}
			else
			{
				if (Cluster == null)
				{
					return false;
				}

				IPerforceServer? Server = await LoadBalancer.SelectServerAsync(Cluster, Agent);
				if (Server == null)
				{
					return false;
				}

				BaseServerAndPort = Server.BaseServerAndPort;
				ServerAndPort = Server.ServerAndPort;
			}

			// Find the matching credentials for the desired user
			PerforceCredentials? Credentials = null;
			if (Cluster != null)
			{
				if (Workspace.UserName == null)
				{
					Credentials = Cluster.Credentials.FirstOrDefault();
				}
				else
				{
					Credentials = Cluster.Credentials.FirstOrDefault(x => String.Equals(x.UserName, Workspace.UserName, StringComparison.OrdinalIgnoreCase));
				}
			}

			// Construct the message
			HordeCommon.Rpc.Messages.AgentWorkspace Result = new HordeCommon.Rpc.Messages.AgentWorkspace();
			Result.ConfiguredCluster = Workspace.Cluster;
			Result.ConfiguredUserName = Workspace.UserName;
			Result.Cluster = Cluster?.Name;
			Result.BaseServerAndPort = BaseServerAndPort;
			Result.ServerAndPort = ServerAndPort;
			Result.UserName = Credentials?.UserName ?? Workspace.UserName;
			Result.Password = Credentials?.Password;
			Result.Identifier = Workspace.Identifier;
			Result.Stream = Workspace.Stream;
			if (Workspace.View != null)
			{
				Result.View.AddRange(Workspace.View);
			}
			Result.Incremental = Workspace.bIncremental;
			WorkspaceMessages.Add(Result);

			return true;
		}
	}
}
