// Copyright Epic Games, Inc. All Rights Reserved.

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
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Query selecting the base changelist to use
	/// </summary>
	public class ChangeQueryRequest
	{
		/// <summary>
		/// The template id to query
		/// </summary>
		public string? TemplateId { get; set; }

		/// <summary>
		/// The target to query
		/// </summary>
		public string? Target { get; set; }

		/// <summary>
		/// Whether to match a job that produced warnings
		/// </summary>
		public List<JobStepOutcome>? Outcomes { get; set; }

		/// <summary>
		/// Convert to a model object
		/// </summary>
		/// <returns></returns>
		public ChangeQuery ToModel()
		{
			ChangeQuery Query = new ChangeQuery();
			if (TemplateId != null)
			{
				Query.TemplateRefId = new StringId<TemplateRef>(TemplateId);
			}
			Query.Target = Target;
			Query.Outcomes = Outcomes;
			return Query;
		}
	}

	/// <summary>
	/// Specifies defaults for running a preflight
	/// </summary>
	public class DefaultPreflightRequest
	{
		/// <summary>
		/// The template id to query
		/// </summary>
		public string? TemplateId { get; set; }

		/// <summary>
		/// The last successful job type to use for the base changelist
		/// </summary>
		[Obsolete("Use Change.TemplateId instead")]
		public string? ChangeTemplateId { get; set; }

		/// <summary>
		/// Query for the change to use
		/// </summary>
		public ChangeQueryRequest? Change { get; set; }

		/// <summary>
		/// Convert to a model object
		/// </summary>
		/// <returns></returns>
		public DefaultPreflight ToModel()
		{
#pragma warning disable CS0618 // Type or member is obsolete
			if (ChangeTemplateId != null)
			{
				Change ??= new ChangeQueryRequest();
				Change.TemplateId = ChangeTemplateId;
			}
			return new DefaultPreflight((TemplateId != null) ? (TemplateRefId?)new TemplateRefId(TemplateId) : null, Change?.ToModel());
#pragma warning restore CS0618 // Type or member is obsolete
		}
	}

	/// <summary>
	/// Mapping from a BuildGraph agent type to a set of machines on the farm
	/// </summary>
	public class CreateAgentTypeRequest
	{
		/// <summary>
		/// Pool of agents to use for this agent type
		/// </summary>
		[Required]
		public string Pool { get; set; } = null!;

		/// <summary>
		/// Name of the workspace to sync
		/// </summary>
		public string? Workspace { get; set; }

		/// <summary>
		/// Path to the temporary storage dir
		/// </summary>
		public string? TempStorageDir { get; set; }

		/// <summary>
		/// Environment variables to be set when executing the job
		/// </summary>
		public Dictionary<string, string>? Environment { get; set; }
	}

	/// <summary>
	/// Information about a workspace type
	/// </summary>
	public class CreateWorkspaceTypeRequest
	{
		/// <summary>
		/// Name of the Perforce server cluster to use
		/// </summary>
		public string? Cluster { get; set; }

		/// <summary>
		/// The Perforce server and port (eg. perforce:1666)
		/// </summary>
		public string? ServerAndPort { get; set; }

		/// <summary>
		/// User to log into Perforce with (defaults to buildmachine)
		/// </summary>
		public string? UserName { get; set; }

		/// <summary>
		/// Password to use to log into the workspace
		/// </summary>
		public string? Password { get; set; }

		/// <summary>
		/// Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.
		/// </summary>
		public string? Identifier { get; set; }

		/// <summary>
		/// Override for the stream to sync
		/// </summary>
		public string? Stream { get; set; }

		/// <summary>
		/// Custom view for the workspace
		/// </summary>
		public List<string>? View { get; set; }

		/// <summary>
		/// Whether to use an incrementally synced workspace
		/// </summary>
		public bool Incremental { get; set; }
	}

	/// <summary>
	/// Trigger for another template
	/// </summary>
	public class CreateChainedJobTemplateRequest
	{
		/// <summary>
		/// Name of the target that needs to complete before starting the other template
		/// </summary>
		[Required]
		public string Trigger { get; set; } = String.Empty;

		/// <summary>
		/// Id of the template to trigger
		/// </summary>
		[Required]
		public string TemplateId { get; set; } = String.Empty;
	}

	/// <summary>
	/// Parameters to create a template within a stream
	/// </summary>
	public class CreateTemplateRefRequest : CreateTemplateRequest
	{
		/// <summary>
		/// Optional identifier for this ref. If not specified, an id will be generated from the name.
		/// </summary>
		public string? Id { get; set; }

		/// <summary>
		/// Whether to show badges in UGS for these jobs
		/// </summary>
		public bool ShowUgsBadges { get; set; }

		/// <summary>
		/// Whether to show alerts in UGS for these jobs
		/// </summary>
		public bool ShowUgsAlerts { get; set; }

		/// <summary>
		/// Notification channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Can be Success|Failure|Warnings
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Triage channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// Schedule to execute this template
		/// </summary>
		public CreateScheduleRequest? Schedule { get; set; }

		/// <summary>
		/// List of chained job triggers
		/// </summary>
		public List<CreateChainedJobTemplateRequest>? ChainedJobs { get; set; }

		/// <summary>
		/// The ACL for this template
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Mapping from a BuildGraph agent type to a set of machines on the farm
	/// </summary>
	public class GetAgentTypeResponse
	{
		/// <summary>
		/// Pool of agents to use for this agent type
		/// </summary>
		public string Pool { get; set; }

		/// <summary>
		/// Name of the workspace to sync
		/// </summary>
		public string? Workspace { get; set; }

		/// <summary>
		/// Path to the temporary storage dir
		/// </summary>
		public string? TempStorageDir { get; set; }

		/// <summary>
		/// Environment variables to be set when executing the job
		/// </summary>
		public Dictionary<string, string>? Environment { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pool">Pool of agents to use for this agent type</param>
		/// <param name="Workspace">Name of the workspace to sync</param>
		/// <param name="TempStorageDir">Path to the temp storage directory</param>
		/// <param name="Environment">Environment variables to be set when executing this job</param>
		public GetAgentTypeResponse(string Pool, string? Workspace, string? TempStorageDir, Dictionary<string, string>? Environment)
		{
			this.Pool = Pool;
			this.Workspace = Workspace;
			this.TempStorageDir = TempStorageDir;
			this.Environment = Environment;
		}
	}

	/// <summary>
	/// Information about a workspace type
	/// </summary>
	public class GetWorkspaceTypeResponse
	{
		/// <summary>
		/// The Perforce server cluster
		/// </summary>
		public string? Cluster { get; set; }

		/// <summary>
		/// The Perforce server and port (eg. perforce:1666)
		/// </summary>
		public string? ServerAndPort { get; set; }

		/// <summary>
		/// User to log into Perforce with (defaults to buildmachine)
		/// </summary>
		public string? UserName { get; set; }

		/// <summary>
		/// Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.
		/// </summary>
		public string? Identifier { get; set; }

		/// <summary>
		/// Override for the stream to sync
		/// </summary>
		public string? Stream { get; set; }

		/// <summary>
		/// Custom view for the workspace
		/// </summary>
		public List<string>? View { get; set; }

		/// <summary>
		/// Whether to use an incrementally synced workspace
		/// </summary>
		public bool Incremental { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public GetWorkspaceTypeResponse()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Cluster">The server cluster</param>
		/// <param name="ServerAndPort">The perforce server</param>
		/// <param name="UserName">The perforce user name</param>
		/// <param name="Identifier">Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.</param>
		/// <param name="Stream">Override for the stream to sync</param>
		/// <param name="View">Custom view for the workspace</param>
		/// <param name="bIncremental">Whether to use an incrementally synced workspace</param>
		public GetWorkspaceTypeResponse(string? Cluster, string? ServerAndPort, string? UserName, string? Identifier, string? Stream, List<string>? View, bool bIncremental)
		{
			this.Cluster = Cluster;
			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.Identifier = Identifier;
			this.Stream = Stream;
			this.View = View;
			this.Incremental = bIncremental;
		}
	}

	/// <summary>
	/// Trigger for another template
	/// </summary>
	public class GetChainedJobTemplateResponse
	{
		/// <summary>
		/// Name of the target that needs to complete before starting the other template
		/// </summary>
		public string Trigger { get; set; }

		/// <summary>
		/// Id of the template to trigger
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Trigger">The trigger definition</param>
		public GetChainedJobTemplateResponse(ChainedJobTemplate Trigger)
		{
			this.Trigger = Trigger.Trigger;
			this.TemplateId = Trigger.TemplateRefId.ToString();
		}
	}

	/// <summary>
	/// Information about a template in this stream
	/// </summary>
	public class GetTemplateRefResponse : GetTemplateResponseBase
	{
		/// <summary>
		/// Id of the template ref
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Hash of the template definition
		/// </summary>
		public string Hash { get; set; }

		/// <summary>
		/// Whether to show badges in UGS for these jobs
		/// </summary>
		public bool ShowUgsBadges { get; set; }

		/// <summary>
		/// Whether to show alerts in UGS for these jobs
		/// </summary>
		public bool ShowUgsAlerts { get; set; }

		/// <summary>
		/// Notification channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Can be Success|Failure|Warnings
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Triage channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// The schedule for this ref
		/// </summary>
		public GetScheduleResponse? Schedule { get; set; }

		/// <summary>
		/// List of templates to trigger
		/// </summary>
		public List<GetChainedJobTemplateResponse>? ChainedJobs { get; set; }

		/// <summary>
		/// ACL for this template
		/// </summary>
		public GetAclResponse? Acl { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">The template ref id</param>
		/// <param name="TemplateRef">The template ref</param>
		/// <param name="Template">The template instance</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		public GetTemplateRefResponse(TemplateRefId Id, TemplateRef TemplateRef, ITemplate Template, bool bIncludeAcl)
			: base(Template)
		{
			this.Id = Id.ToString();
			Hash = TemplateRef.Hash.ToString();
			ShowUgsBadges = TemplateRef.ShowUgsBadges;
			ShowUgsAlerts = TemplateRef.ShowUgsAlerts;
			NotificationChannel = TemplateRef.NotificationChannel;
			NotificationChannelFilter = TemplateRef.NotificationChannelFilter;
			Schedule = (TemplateRef.Schedule != null) ? new GetScheduleResponse(TemplateRef.Schedule) : null;
			ChainedJobs = (TemplateRef.ChainedJobs != null && TemplateRef.ChainedJobs.Count > 0) ? TemplateRef.ChainedJobs.ConvertAll(x => new GetChainedJobTemplateResponse(x)) : null;
			Acl = (bIncludeAcl && TemplateRef.Acl != null)? new GetAclResponse(TemplateRef.Acl) : null;
		}
	}

	/// <summary>
	/// Response describing a stream
	/// </summary>
	public class GetStreamResponse
	{
		/// <summary>
		/// Unique id of the stream
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Unique id of the project containing this stream
		/// </summary>
		public string ProjectId { get; set; }

		/// <summary>
		/// Name of the stream
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The config file path on the server
		/// </summary>
		public string ConfigPath { get; set; } = String.Empty;

		/// <summary>
		/// Revision of the config file 
		/// </summary>
		public string ConfigRevision { get; set; } = String.Empty;

		/// <summary>
		/// Order to display in the list
		/// </summary>
		public int Order { get; set; }

		/// <summary>
		/// Notification channel for all jobs in this stream
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Can be Success|Failure|Warnings
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Channel to post issue triage notifications
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// Default template for running preflights
		/// </summary>
		public string? DefaultPreflightTemplate { get; set; }

		/// <summary>
		/// Default template to use for preflights
		/// </summary>
		public DefaultPreflightRequest? DefaultPreflight { get; set; }

		/// <summary>
		/// List of tabs to display for this stream
		/// </summary>
		public List<GetStreamTabResponse> Tabs { get; set; }

		/// <summary>
		/// Map of agent name to type
		/// </summary>
		public Dictionary<string, GetAgentTypeResponse> AgentTypes { get; set; }

		/// <summary>
		/// Map of workspace name to type
		/// </summary>
		public Dictionary<string, GetWorkspaceTypeResponse>? WorkspaceTypes { get; set; }

		/// <summary>
		/// Templates for jobs in this stream
		/// </summary>
		public List<GetTemplateRefResponse> Templates { get; set; }

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public GetAclResponse? Acl { get; set; }
		
		/// <summary>
		/// Stream paused for new builds until this date
		/// </summary>
		public DateTime? PausedUntil { get; set; }
		
		/// <summary>
		/// Reason for stream being paused
		/// </summary>
		public string? PauseComment { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id of the stream</param>
		/// <param name="ProjectId">Unique id of the project containing the stream</param>
		/// <param name="Name">Name of the stream</param>
		/// <param name="ConfigPath">Path to the config file for this stream</param>
		/// <param name="ConfigRevision">The config file path on the server</param>
		/// <param name="Order">Order to display this stream</param>
		/// <param name="NotificationChannel"></param>
		/// <param name="NotificationChannelFilter"></param>
		/// <param name="TriageChannel"></param>
		/// <param name="DefaultPreflight">The default template to use for preflights</param>
		/// <param name="Tabs">List of tabs to display for this stream</param>
		/// <param name="AgentTypes">Map of agent type name to description</param>
		/// <param name="WorkspaceTypes">Map of workspace name to description</param>
		/// <param name="Templates">Templates for this stream</param>
		/// <param name="Acl">Permissions for this object</param>
		/// <param name="PausedUntil">Stream paused for new builds until this date</param>
		/// <param name="PauseComment">Reason for stream being paused</param>
		public GetStreamResponse(string Id, string ProjectId, string Name, string ConfigPath, string ConfigRevision, int Order, string? NotificationChannel, string? NotificationChannelFilter, string? TriageChannel, DefaultPreflightRequest? DefaultPreflight, List<GetStreamTabResponse> Tabs, Dictionary<string, GetAgentTypeResponse> AgentTypes, Dictionary<string, GetWorkspaceTypeResponse>? WorkspaceTypes, List<GetTemplateRefResponse> Templates, GetAclResponse? Acl, DateTime? PausedUntil, string? PauseComment)
		{
			this.Id = Id;
			this.ProjectId = ProjectId;
			this.Name = Name;
			this.ConfigPath = ConfigPath;
			this.ConfigRevision = ConfigRevision;
			this.Order = Order;
			this.NotificationChannel = NotificationChannel;
			this.NotificationChannelFilter = NotificationChannelFilter;
			this.TriageChannel = TriageChannel;
			this.DefaultPreflightTemplate = DefaultPreflight?.TemplateId;
			this.DefaultPreflight = DefaultPreflight;
			this.Tabs = Tabs;
			this.AgentTypes = AgentTypes;
			this.WorkspaceTypes = WorkspaceTypes;
			this.Templates = Templates;
			this.Acl = Acl;
			this.PausedUntil = PausedUntil;
			this.PauseComment = PauseComment;
		}
	}
}
