// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Parameters to register a new agent
	/// </summary>
	public class CreateAgentRequest
	{
		/// <summary>
		/// Friendly name for the agent
		/// </summary>
		[Required]
		public string Name { get; set; } = null!; // Enforced by [required] attribute

		/// <summary>
		/// Whether the agent is currently enabled
		/// </summary>
		public bool Enabled { get; set; } = true;

		/// <summary>
		/// Whether the agent is ephemeral (ie. should not be shown when inactive)
		/// </summary>
		public bool Ephemeral { get; set; }

		/// <summary>
		/// Per-agent override for the desired agent software channel
		/// </summary>
		public string? Channel { get; set; }

		/// <summary>
		/// Pools for this agent
		/// </summary>
		public List<string>? Pools { get; set; }
	}

	/// <summary>
	/// Response from creating an agent
	/// </summary>
	public class CreateAgentResponse
	{
		/// <summary>
		/// Unique id for the new agent
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id for this agent</param>
		public CreateAgentResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Parameters to update an agent
	/// </summary>
	public class UpdateAgentRequest
	{
		/// <summary>
		/// Whether the agent is currently enabled
		/// </summary>
		public bool? Enabled { get; set; }

		/// <summary>
		/// Whether the agent is ephemeral
		/// </summary>
		public bool? Ephemeral { get; set; }

		/// <summary>
		/// Request a conform be performed using the current agent set
		/// </summary>
		public bool? RequestConform { get; set; }

		/// <summary>
		/// Request the machine be restarted
		/// </summary>
		public bool? RequestRestart { get; set; }

		/// <summary>
		/// Request the machine be shut down
		/// </summary>
		public bool? RequestShutdown { get; set; }

		/// <summary>
		/// Per-agent override for the desired agent software channel
		/// </summary>
		public string? Channel { get; set; }

		/// <summary>
		/// Pools for this agent
		/// </summary>
		public List<string>? Pools { get; set; }

		/// <summary>
		/// New ACL for this agent
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
		
		/// <summary>
		/// New comment
		/// </summary>
		public string? Comment { get; set; }
	}

	/// <summary>
	/// Response for queries to find a particular lease within an agent
	/// </summary>
	public class GetAgentLeaseResponse
	{
		/// <summary>
		/// Identifier for the lease
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// The agent id
		/// </summary>
		public string? AgentId { get; set; }

		/// <summary>
		/// Name of the lease
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Log id for this lease
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// Time at which the lease started (UTC)
		/// </summary>
		public DateTime StartTime { get; set; }

		/// <summary>
		/// Time at which the lease started (UTC)
		/// </summary>
		public DateTime? FinishTime { get; set; }

		/// <summary>
		/// Whether this lease has started executing on the agent yet
		/// </summary>
		public bool Executing { get; set; }

		/// <summary>
		/// Details of the payload being executed
		/// </summary>
		public Dictionary<string, string>? Details { get; set; }

		/// <summary>
		/// Outcome of the lease
		/// </summary>
		public LeaseOutcome? Outcome { get; set; }

		/// <summary>
		/// State of the lease (for AgentLeases)
		/// </summary>
		public LeaseState? State { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Lease">The lease to initialize from</param>
		public GetAgentLeaseResponse(AgentLease Lease)
		{
			this.Id = Lease.Id.ToString();
			this.Name = Lease.Name;
			this.LogId = Lease.LogId?.ToString();
			this.State = Lease.State;
			this.StartTime = Lease.StartTime;
			this.Executing = Lease.Active;
			this.FinishTime = Lease.ExpiryTime;
			this.Details = AgentLease.GetPayloadDetails(Lease.Payload);			
		}

		/// <summary>
		/// Converts this lease to a response object
		/// </summary>
		/// <param name="Lease">The lease to initialize from</param>
		public GetAgentLeaseResponse(ILease Lease)
		{
			this.Id = Lease.Id.ToString();
			this.AgentId = Lease.AgentId.ToString();
			this.Name = Lease.Name;
			this.LogId = Lease.LogId?.ToString();
			this.StartTime = Lease.StartTime;
			this.Executing = (Lease.FinishTime == null);
			this.FinishTime = Lease.FinishTime;
			this.Details = AgentLease.GetPayloadDetails(Lease.Payload);
			this.Outcome = Lease.Outcome;
		}
	}

	/// <summary>
	/// Information about an agent session
	/// </summary>
	public class GetAgentSessionResponse
	{
		/// <summary>
		/// Unique id for this session
		/// </summary>
		[Required]
		public string Id { get; set; }

		/// <summary>
		/// Start time for this session
		/// </summary>
		[Required]
		public DateTime StartTime { get; set; }

		/// <summary>
		/// Finishing time for this session
		/// </summary>
		public DateTime? FinishTime { get; set; }

		/// <summary>
		/// Properties of this agent
		/// </summary>
		public List<string>? Properties { get; set; }

		/// <summary>
		/// Version of the software running during this session
		/// </summary>
		public string? Version { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Session">The session to construct from</param>
		public GetAgentSessionResponse(ISession Session)
		{
			this.Id = Session.Id.ToString();
			this.StartTime = Session.StartTime;
			this.FinishTime = Session.FinishTime;
			this.Properties = (Session.Properties != null) ? new List<string>(Session.Properties) : null;
			this.Version = Session.Version?.ToString();
		}
	}

	/// <summary>
	/// Information about a workspace synced on an agent
	/// </summary>
	public class GetAgentWorkspaceResponse
	{
		/// <summary>
		/// The Perforce server and port to connect to
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
		/// <param name="Workspace">The workspace to construct from</param>
		public GetAgentWorkspaceResponse(AgentWorkspace Workspace)
		{
			this.Cluster = Workspace.Cluster;
			this.UserName = Workspace.UserName;
			this.Identifier = Workspace.Identifier;
			this.Stream = Workspace.Stream;
			this.View = Workspace.View;
			this.bIncremental = Workspace.bIncremental;
		}
	}

	/// <summary>
	/// Information about an agent
	/// </summary>
	public class GetAgentResponse
	{
		/// <summary>
		/// The agent's unique ID
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Friendly name of the agent
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Whether the agent is currently enabled
		/// </summary>
		public bool Enabled { get; set; }

		/// <summary>
		/// The current session id
		/// </summary>
		public string? SessionId { get; set; }

		/// <summary>
		/// Whether the agent is ephemeral
		/// </summary>
		public bool Ephemeral { get; set; }

		/// <summary>
		/// Whether the agent is currently online
		/// </summary>
		public bool Online { get; set; }

		/// <summary>
		/// Whether this agent has expired
		/// </summary>
		public bool Deleted { get; set; }

		/// <summary>
		/// Whether a conform job is pending
		/// </summary>
		public bool PendingConform { get; set; }

		/// <summary>
		/// Whether a restart is pending
		/// </summary>
		public bool PendingRestart { get; set; }
		
		/// <summary>
		/// Whether a shutdown is pending
		/// </summary>
		public bool PendingShutdown { get; set; }

		/// <summary>
		/// Last time a conform was attempted
		/// </summary>
		public DateTime LastConformTime { get; set; }

		/// <summary>
		/// Number of times a conform has been attempted
		/// </summary>
		public int? ConformAttemptCount { get; set; }

		/// <summary>
		/// Last time a conform was attempted
		/// </summary>
		public DateTime? NextConformTime { get; set; }

		/// <summary>
		/// The current client version
		/// </summary>
		public string? Version { get; set; }

		/// <summary>
		/// Per-agent override for the desired agent software channel
		/// </summary>
		public string? Channel { get; set; }

		/// <summary>
		/// Properties for the agent
		/// </summary>
		public List<string> Properties { get; set; }

		/// <summary>
		/// Resources for the agent
		/// </summary>
		public Dictionary<string, int> Resources { get; set; }

		/// <summary>
		/// Last update time of this agent
		/// </summary>
		public DateTime? UpdateTime { get; set; }

		/// <summary>
		/// Pools for this agent
		/// </summary>
		public List<string>? Pools { get; set; }

		/// <summary>
		/// List of workspaces currently synced to this machine
		/// </summary>
		public List<GetAgentWorkspaceResponse> Workspaces { get; set; } = new List<GetAgentWorkspaceResponse>();

		/// <summary>
		/// Capabilities of this agent
		/// </summary>
		public object? Capabilities { get; }

		/// <summary>
		/// Array of active leases.
		/// </summary>
		public List<GetAgentLeaseResponse> Leases { get; }

		/// <summary>
		/// Per-object permissions
		/// </summary>
		public GetAclResponse? Acl { get; set; }
		
		/// <summary>
		/// Comment for this agent
		/// </summary>
		public string? Comment { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Agent">The agent to construct from</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		public GetAgentResponse(IAgent Agent, bool bIncludeAcl)
		{
			this.Id = Agent.Id.ToString();
			this.Name = Agent.Id.ToString();
			this.Enabled = Agent.Enabled;
			this.Properties = new List<string>(Agent.Properties);
			this.Resources = new Dictionary<string, int>(Agent.Resources);
			this.SessionId = Agent.SessionId?.ToString();
			this.Online = Agent.IsSessionValid(DateTime.UtcNow);
			this.Deleted = Agent.Deleted;
			this.PendingConform = Agent.RequestConform;
			this.PendingRestart = Agent.RequestRestart;
			this.PendingShutdown = Agent.RequestShutdown;
			this.LastConformTime = Agent.LastConformTime;
			this.NextConformTime =   Agent.LastConformTime;
			this.ConformAttemptCount = Agent.ConformAttemptCount;
			this.Version = Agent.Version?.ToString() ?? "Unknown";
			if(Agent.Channel != null)
			{
				this.Version += $" ({Agent.Channel})";
			}
			this.Version = Agent.Version?.ToString();
			this.Channel = Agent.Channel?.ToString();
			this.UpdateTime = Agent.UpdateTime;
			this.Pools = Agent.GetPools().Select(x => x.ToString()).ToList();
			this.Workspaces = Agent.Workspaces.ConvertAll(x => new GetAgentWorkspaceResponse(x));
			this.Capabilities = new { Devices = new[] { new { Properties = Agent.Properties, Resources = Agent.Resources } } };
			this.Leases = Agent.Leases.ConvertAll(x => new GetAgentLeaseResponse(x));
			this.Acl = (bIncludeAcl && Agent.Acl != null) ? new GetAclResponse(Agent.Acl) : null;
			this.Comment = Agent.Comment;
		}
	}
}
