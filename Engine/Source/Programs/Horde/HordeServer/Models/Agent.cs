// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
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
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;
using AgentSoftwareChannelName = HordeServer.Utilities.StringId<HordeServer.Services.AgentSoftwareChannels>;
using System.Diagnostics;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Information about a workspace synced to an agent
	/// </summary>
	public class AgentWorkspace
	{
		/// <summary>
		/// Identifier for the AutoSDK workspace
		/// </summary>
		public const string AutoSdkIdentifier = "AutoSDK";

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
	/// Information about a device allocated for a lease
	/// </summary>
	public class AgentLeaseDevice
	{
		/// <summary>
		/// Index of the device in the agent's device list
		/// </summary>
		[BsonRequired]
		public int Index { get; set; }

		/// <summary>
		/// Handle of the device in the lease, ie. the logical name in the context of the work being done, not the physical device name.
		/// </summary>
		[BsonRequired]
		public string? Handle { get; set; }

		/// <summary>
		/// Resources claimed by the lease. If null, the whole device is claimed, and nothing else can be allocated to it.
		/// </summary>
		[BsonIgnoreIfNull]
		public Dictionary<string, int>? Resources { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		[BsonConstructor]
		private AgentLeaseDevice()
		{
			Handle = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Index"></param>
		/// <param name="Handle"></param>
		/// <param name="Resources"></param>
		public AgentLeaseDevice(int Index, string? Handle, Dictionary<string, int>? Resources)
		{
			this.Index = Index;
			this.Handle = Handle;
			this.Resources = Resources;
		}
	}

	/// <summary>
	/// Document describing an active lease
	/// </summary>
	public class AgentLease
	{
		/// <summary>
		/// Name of this lease
		/// </summary>
		[BsonRequired]
		public ObjectId Id { get; set; }

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
		public ObjectId? LogId { get; set; }

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
		/// Requirements for this lease
		/// </summary>
		public AgentRequirements Requirements { get; set; }

		/// <summary>
		/// For leases in the pending state, encodes an "any" protobuf containing the payload for the agent to execute the lease.
		/// </summary>
		public byte[]? Payload { get; set; }

		/// <summary>
		/// List of devices allocated to this lease
		/// </summary>
		[BsonIgnoreIfNull]
		public List<AgentLeaseDevice>? Devices { get; set; }

		/// <summary>
		/// Private constructor
		/// </summary>
		[BsonConstructor]
		private AgentLease()
		{
			Name = String.Empty;
			Requirements = null!;
			Devices = null!;
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
		/// <param name="Payload">Encoded "any" protobuf describing the contents of the payload</param>
		/// <param name="Requirements">Requirements for this lease</param>
		/// <param name="Devices">Device mapping for this lease</param>
		public AgentLease(ObjectId Id, string Name, StreamId? StreamId, PoolId? PoolId, ObjectId? LogId, LeaseState State, byte[]? Payload, AgentRequirements Requirements, List<AgentLeaseDevice>? Devices)
		{
			this.Id = Id;
			this.Name = Name;
			this.StreamId = StreamId;
			this.PoolId = PoolId;
			this.LogId = LogId;
			this.State = State;
			this.Payload = Payload;
			this.Requirements = Requirements;
			this.Devices = Devices;
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
		/// Arbitrary comment for the agent (useful for disable reasons etc)
		/// </summary>
		public string? Comment { get; }

		/// <summary>
		/// Whether the agent is ephemeral
		/// </summary>
		public bool Ephemeral { get; }

		/// <summary>
		/// Whether the agent should be included on the dashboard. This is set to true for ephemeral agents once they are no longer online, or agents that are explicitly deleted.
		/// </summary>
		public bool Deleted { get; }

		/// <summary>
		/// Version of the software running on this agent
		/// </summary>
		public AgentSoftwareVersion? Version { get; }

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
		/// Capabilities of this agent
		/// </summary>
		public AgentCapabilities Capabilities { get; }

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
		/// Gets a list of pools for the given agent. Includes all automatically assigned pools based on agent capabilities.
		/// </summary>
		/// <param name="Agent">The agent instance</param>
		/// <param name="Pools">List of all available pools</param>
		/// <returns>List of pools</returns>
		public static IEnumerable<IPool> GetPools(this IAgent Agent, IEnumerable<IPool> Pools)
		{
			return Pools.Where(x => Agent.InPool(x));
		}

		/// <summary>
		/// Checks whether an agent is in the given pool
		/// </summary>
		/// <param name="Agent">The agent to check</param>
		/// <param name="Pool">The pool to test against</param>
		/// <returns>True if the agent is in the pool</returns>
		public static bool InPool(this IAgent Agent, IPool Pool)
		{
			if (Agent.ExplicitPools.Contains(Pool.Id))
			{
				return true;
			}
			else if (Pool.Requirements != null && AgentExtensions.TryCreateLease(Agent.Capabilities, Pool.Requirements, Enumerable.Empty<AgentLease>(), out _))
			{
				return true;
			}
			return false;
		}

		/// <summary>
		/// Determine whether it's possible to add a lease for the given resources
		/// </summary>
		/// <param name="Agent">The agent to create a lease for</param>
		/// <param name="Pool">The pool required</param>
		/// <param name="Requirements">Requirements for this lease</param>
		/// <param name="LeasedDevices">On success, recieves the list of leased devices</param>
		/// <returns>True if the new lease can be granted</returns>
		public static bool TryCreateLease(this IAgent Agent, IPool Pool, AgentRequirements? Requirements, out List<AgentLeaseDevice>? LeasedDevices)
		{
			if (!Agent.Enabled || Agent.Status != AgentStatus.Ok || !Agent.InPool(Pool))
			{
				LeasedDevices = null!;
				return false;
			}
			return TryCreateLease(Agent.Capabilities, Requirements, Agent.Leases, out LeasedDevices);
		}

		/// <summary>
		/// Attempts to match the requirements for a particular agent, and return the list of leased devices that fulfils the requirements
		/// </summary>
		/// <param name="Capabilities">Capabilities of the gent</param>
		/// <param name="Requirements">Requirements for the lease</param>
		/// <param name="Leases">List of current leases</param>
		/// <param name="LeasedDevices">Receives the list of device leases</param>
		/// <returns>True if successful</returns>
		public static bool TryCreateLease(AgentCapabilities Capabilities, AgentRequirements? Requirements, IEnumerable<AgentLease> Leases, out List<AgentLeaseDevice>? LeasedDevices)
		{
			List<AgentLeaseDevice>? Result = null;
			if (Requirements != null)
			{
				if (!Requirements.Shared && Leases.Any())
				{
					LeasedDevices = null;
					return false;
				}

				if (Requirements.Properties != null)
				{
					foreach (string Property in Requirements.Properties)
					{
						if (Capabilities.Properties == null || !Capabilities.Properties.Contains(Property))
						{
							LeasedDevices = null!;
							return false;
						}
					}
				}

				if(Requirements.Devices != null)
				{
					if (!TryCreateDeviceLeases(Capabilities.Devices, Requirements.Devices, 0, 0, Leases.SelectMany(x => x.Devices), out Result))
					{
						LeasedDevices = null!;
						return false;
					}
				}
			}

			LeasedDevices = Result;
			return true;
		}

		/// <summary>
		/// Try to match up the required devices for a lease to the available devices on an agent. This is done through a recursive search with backtracking for completeness,
		/// due to not knowing which devices in the lease will correspond to each device in the agent, but it should be quick in practice due to agents having a small number 
		/// of homogenous devices.
		/// </summary>
		/// <param name="CapableDevices">Device capabilities for the agent</param>
		/// <param name="RequiredDevices">Device requirements for the lease</param>
		/// <param name="RequiredDeviceIdx">Current index of the device in the lease requirements</param>
		/// <param name="LeasedDeviceMask">Bitmask for devices that have currently been allocated. This is limited to 32, which is unlikely to be a problem in practice.</param>
		/// <param name="CurrentLeasedDevices">The current set of leased devices</param>
		/// <param name="LeasedDevices">On success, receives a list of the leased devices, mapping each requested device to a device in the agent device array.</param>
		/// <returns>True if the leases were allocated</returns>
		private static bool TryCreateDeviceLeases(List<DeviceCapabilities> CapableDevices, List<DeviceRequirements> RequiredDevices, int RequiredDeviceIdx, uint LeasedDeviceMask, IEnumerable<AgentLeaseDevice> CurrentLeasedDevices, [MaybeNullWhen(false)] out List<AgentLeaseDevice> LeasedDevices)
		{
			// If we've assigned all the required devices now, we can create the list of assigned devices
			if (RequiredDeviceIdx == RequiredDevices.Count)
			{
				LeasedDevices = new List<AgentLeaseDevice>(RequiredDevices.Count);
				foreach(DeviceRequirements Device in RequiredDevices)
				{
					LeasedDevices.Add(new AgentLeaseDevice(-1, Device.Handle, Device.Resources));
				}
				return true;
			}

			// Otherwise try to assign the next device
			DeviceRequirements RequiredDevice = RequiredDevices[RequiredDeviceIdx];
			for (int DeviceIdx = 0; DeviceIdx < CapableDevices.Count; DeviceIdx++)
			{
				uint DeviceFlag = 1U << DeviceIdx;
				if ((LeasedDeviceMask & DeviceFlag) == 0 && MatchDevice(CapableDevices[DeviceIdx], RequiredDevice, CurrentLeasedDevices.Where(x => x.Index == DeviceIdx)))
				{
					List<AgentLeaseDevice>? Result;
					if (TryCreateDeviceLeases(CapableDevices, RequiredDevices, RequiredDeviceIdx + 1, LeasedDeviceMask | DeviceFlag, CurrentLeasedDevices, out Result))
					{
						LeasedDevices = Result;
						LeasedDevices[RequiredDeviceIdx].Index = DeviceIdx;
						return true;
					}
				}
			}

			// Otherwise failed
			LeasedDevices = null!;
			return false;
		}

		/// <summary>
		/// Determines if an agent device has the given requirements
		/// </summary>
		/// <param name="Capabilities">The device capabilities</param>
		/// <param name="Requirements">Requirements for the lease</param>
		/// <param name="Leases">Current leases for the device</param>
		/// <returns>True if the device can satisfy the given requirements</returns>
		private static bool MatchDevice(DeviceCapabilities Capabilities, DeviceRequirements Requirements, IEnumerable<AgentLeaseDevice> Leases)
		{
			// Check the device has all the required properties
			if (Requirements.Properties != null)
			{
				foreach (string Property in Requirements.Properties)
				{
					if (Capabilities.Properties == null || !Capabilities.Properties.Contains(Property))
					{
						return false;
					}
				}
			}

			// Check the device has all the required resources
			if (Requirements.Resources == null)
			{
				// Requires exclusive access to the device
				if (Leases.Any())
				{
					return false;
				}
			}
			else
			{
				// Requires shared access to the device. Check the device can be shared.
				if (Capabilities.Resources == null)
				{
					return false;
				}

				// Check there are enough available of each resource type
				foreach (KeyValuePair<string, int> Resource in Requirements.Resources)
				{
					// Make sure the device has enough of the named resource to start with
					int RemainingCount;
					if (!Capabilities.Resources.TryGetValue(Resource.Key, out RemainingCount) || RemainingCount < Resource.Value)
					{
						return false;
					}

					// Check each existing lease of this device
					foreach (AgentLeaseDevice Lease in Leases)
					{
						// If the lease has an exclusive reservation, we can't use it
						if (Lease.Resources == null)
						{
							return false;
						}

						// Update the remaining count for this resource
						int UsedCount;
						if (Lease.Resources.TryGetValue(Resource.Key, out UsedCount))
						{
							RemainingCount -= UsedCount;
							if (RemainingCount < Resource.Value)
							{
								return false;
							}
						}
					}
				}
			}

			return true;
		}

		/// <summary>
		/// Converts this workspace to an RPC message
		/// </summary>
		/// <param name="Agent">The agent to get a workspace for</param>
		/// <param name="Workspace">The workspace definition</param>
		/// <param name="Globals">The global state</param>
		/// <param name="LoadBalancer">The Perforce load balancer</param>
		/// <param name="WorkspaceMessages">List of messages</param>
		/// <returns>The RPC message</returns>
		public static async Task<bool> TryAddWorkspaceMessage(this IAgent Agent, AgentWorkspace Workspace, Globals Globals, PerforceLoadBalancer LoadBalancer, IList<HordeCommon.Rpc.Messages.AgentWorkspace> WorkspaceMessages)
		{
			// Find the matching Perforce cluster
			PerforceCluster? ClusterInfo;
			if (Workspace.Cluster == null)
			{
				ClusterInfo = Globals.PerforceClusters.FirstOrDefault();
			}
			else
			{
				ClusterInfo = Globals.PerforceClusters.FirstOrDefault(x => String.Equals(x.Name, Workspace.Cluster, StringComparison.OrdinalIgnoreCase));
			}

			// Find a matching server, trying to use a previously selected one if possible
			string? ServerAndPort = WorkspaceMessages.FirstOrDefault(x => x.ConfiguredCluster == Workspace.Cluster)?.ServerAndPort;
			if (ServerAndPort == null)
			{
				if (ClusterInfo == null)
				{
					return false;
				}

				IPerforceServer? Server = await LoadBalancer.SelectServerAsync(ClusterInfo, Agent);
				if(Server == null)
				{
					return false;
				}
				ServerAndPort = Server.ServerAndPort;
			}

			// Find the matching credentials for the desired user
			PerforceCredentials? Credentials = null;
			if (ClusterInfo != null)
			{
				if (Workspace.UserName == null)
				{
					Credentials = ClusterInfo.Credentials.FirstOrDefault();
				}
				else
				{
					Credentials = ClusterInfo.Credentials.FirstOrDefault(x => String.Equals(x.UserName, Workspace.UserName, StringComparison.OrdinalIgnoreCase));
				}
			}

			// Construct the message
			HordeCommon.Rpc.Messages.AgentWorkspace Result = new HordeCommon.Rpc.Messages.AgentWorkspace();
			Result.ConfiguredCluster = Workspace.Cluster;
			Result.ConfiguredUserName = Workspace.UserName;
			Result.Cluster = ClusterInfo?.Name;
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
