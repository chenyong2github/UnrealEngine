// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using EpicGames.Core;
using Horde.Build.Acls;
using Horde.Build.Jobs.Templates;
using Horde.Build.Jobs.Schedules;
using Horde.Build.Users;
using Horde.Build.Utilities;
using Horde.Build.Issues;
using Horde.Build.Perforce;
using Horde.Build.Projects;
using Horde.Build.Jobs;
using Microsoft.AspNetCore.DataProtection;

namespace Horde.Build.Streams
{
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;
	using ProjectId = StringId<IProject>;
	using TemplateId = StringId<ITemplateRef>;

	/// <summary>
	/// Step state update request
	/// </summary>
	public class UpdateStepStateRequest
	{
		/// <summary>
		/// Name of the step
		/// </summary>
		[Required]
		public string Name { get; set; } = String.Empty;

		/// <summary>
		/// User who paused the step
		/// </summary>
		public string? PausedByUserId { get; set; }
	}

	/// <summary>
	/// Updates an existing stream template ref
	/// </summary>
	public class UpdateTemplateRefRequest
	{
		/// <summary>
		/// Step states to update
		/// </summary>
		public List<UpdateStepStateRequest>? StepStates { get; set; }
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
		/// <param name="pool">Pool of agents to use for this agent type</param>
		/// <param name="workspace">Name of the workspace to sync</param>
		/// <param name="tempStorageDir">Path to the temp storage directory</param>
		/// <param name="environment">Environment variables to be set when executing this job</param>
		public GetAgentTypeResponse(string pool, string? workspace, string? tempStorageDir, Dictionary<string, string>? environment)
		{
			Pool = pool;
			Workspace = workspace;
			TempStorageDir = tempStorageDir;
			Environment = environment;
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
		/// Whether to use the AutoSDK
		/// </summary>
		public bool UseAutoSdk { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public GetWorkspaceTypeResponse()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="cluster">The server cluster</param>
		/// <param name="serverAndPort">The perforce server</param>
		/// <param name="userName">The perforce user name</param>
		/// <param name="identifier">Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.</param>
		/// <param name="stream">Override for the stream to sync</param>
		/// <param name="view">Custom view for the workspace</param>
		/// <param name="bIncremental">Whether to use an incrementally synced workspace</param>
		/// <param name="bUseAutoSdk">Whether to use the AutoSDK</param>
		public GetWorkspaceTypeResponse(string? cluster, string? serverAndPort, string? userName, string? identifier, string? stream, List<string>? view, bool bIncremental, bool bUseAutoSdk)
		{
			Cluster = cluster;
			ServerAndPort = serverAndPort;
			UserName = userName;
			Identifier = identifier;
			Stream = stream;
			View = view;
			Incremental = bIncremental;
			UseAutoSdk = bUseAutoSdk;
		}
	}

	/// <summary>
	/// State information for a step in the stream
	/// </summary>
	public class GetTemplateStepStateResponse
	{
		/// <summary>
		/// Name of the step
		/// </summary>
		public string Name { get; set; } = String.Empty;

		/// <summary>
		/// User who paused the step
		/// </summary>
		public GetThinUserInfoResponse? PausedByUserInfo { get; set; }

		/// <summary>
		/// The UTC time when the step was paused
		/// </summary>
		public DateTime? PauseTimeUtc { get; set; }

		/// <summary>
		/// Default constructor for serialization
		/// </summary>
		private GetTemplateStepStateResponse()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public GetTemplateStepStateResponse(ITemplateStep state, GetThinUserInfoResponse? pausedByUserInfo)
		{
			Name = state.Name;
			PauseTimeUtc = state.PauseTimeUtc;
			PausedByUserInfo = pausedByUserInfo;
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
		public List<ChainedJobTemplateConfig>? ChainedJobs { get; set; }

		/// <summary>
		/// List of step states
		/// </summary>
		public List<GetTemplateStepStateResponse>? StepStates { get; set; }

		/// <summary>
		/// List of queries for the default changelist
		/// </summary>
		public List<ChangeQueryConfig>? DefaultChange { get; set; }

		/// <summary>
		/// ACL for this template
		/// </summary>
		public GetAclResponse? Acl { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">The template ref id</param>
		/// <param name="templateRef">The template ref</param>
		/// <param name="template">The template instance</param>
		/// <param name="stepStates">The template step states</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		public GetTemplateRefResponse(TemplateId id, ITemplateRef templateRef, ITemplate template, List<GetTemplateStepStateResponse>? stepStates, bool bIncludeAcl)
			: base(template)
		{
			Id = id.ToString();
			Hash = templateRef.Hash.ToString();
			ShowUgsBadges = templateRef.Config.ShowUgsBadges;
			ShowUgsAlerts = templateRef.Config.ShowUgsAlerts;
			NotificationChannel = templateRef.Config.NotificationChannel;
			NotificationChannelFilter = templateRef.Config.NotificationChannelFilter;
			Schedule = (templateRef.Schedule != null) ? new GetScheduleResponse(templateRef.Schedule) : null;
			ChainedJobs = (templateRef.Config.ChainedJobs != null && templateRef.Config.ChainedJobs.Count > 0) ? templateRef.Config.ChainedJobs : null;
			StepStates = stepStates;
			DefaultChange = templateRef.Config.DefaultChange;
			Acl = (bIncludeAcl && templateRef.Acl != null) ? new GetAclResponse(templateRef.Acl) : null;
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
		public DefaultPreflightConfig? DefaultPreflight { get; set; }

		/// <summary>
		/// List of tabs to display for this stream
		/// </summary>
		public List<TabConfig> Tabs { get; set; }

		/// <summary>
		/// Map of agent name to type
		/// </summary>
		public Dictionary<string, AgentConfig> AgentTypes { get; set; }

		/// <summary>
		/// Map of workspace name to type
		/// </summary>
		public Dictionary<string, WorkspaceConfig>? WorkspaceTypes { get; set; }

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
		/// Workflows for this stream
		/// </summary>
		public List<WorkflowConfig> Workflows { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Unique id of the stream</param>
		/// <param name="projectId">Unique id of the project containing the stream</param>
		/// <param name="name">Name of the stream</param>
		/// <param name="configRevision">The config file path on the server</param>
		/// <param name="order">Order to display this stream</param>
		/// <param name="notificationChannel"></param>
		/// <param name="notificationChannelFilter"></param>
		/// <param name="triageChannel"></param>
		/// <param name="defaultPreflight">The default template to use for preflights</param>
		/// <param name="tabs">List of tabs to display for this stream</param>
		/// <param name="agentTypes">Map of agent type name to description</param>
		/// <param name="workspaceTypes">Map of workspace name to description</param>
		/// <param name="templates">Templates for this stream</param>
		/// <param name="acl">Permissions for this object</param>
		/// <param name="pausedUntil">Stream paused for new builds until this date</param>
		/// <param name="pauseComment">Reason for stream being paused</param>
		/// <param name="workflows">Workflows for this stream</param>
		public GetStreamResponse(string id, string projectId, string name, string configRevision, int order, string? notificationChannel, string? notificationChannelFilter, string? triageChannel, DefaultPreflightConfig? defaultPreflight, List<TabConfig> tabs, Dictionary<string, AgentConfig> agentTypes, Dictionary<string, WorkspaceConfig>? workspaceTypes, List<GetTemplateRefResponse> templates, GetAclResponse? acl, DateTime? pausedUntil, string? pauseComment, List<WorkflowConfig> workflows)
		{
			Id = id;
			ProjectId = projectId;
			Name = name;
			ConfigRevision = configRevision;
			Order = order;
			NotificationChannel = notificationChannel;
			NotificationChannelFilter = notificationChannelFilter;
			TriageChannel = triageChannel;
			DefaultPreflightTemplate = defaultPreflight?.TemplateId?.ToString();
			DefaultPreflight = defaultPreflight;
			Tabs = tabs;
			AgentTypes = agentTypes;
			WorkspaceTypes = workspaceTypes;
			Templates = templates;
			Acl = acl;
			PausedUntil = pausedUntil;
			PauseComment = pauseComment;
			Workflows = workflows;
		}
	}

	/// <summary>
	/// Response describing a stream
	/// </summary>
	public class GetStreamResponseV2
	{
		readonly IStream _stream;

		/// <inheritdoc cref="IStream.Id"/>
		public StreamId Id => _stream.Id;

		/// <inheritdoc cref="IStream.ProjectId"/>
		public ProjectId ProjectId => _stream.ProjectId;

		/// <inheritdoc cref="IStream.Name"/>
		public string Name => _stream.Name;

		/// <inheritdoc cref="IStream.Name"/>
		public string ConfigRevision => _stream.ConfigRevision;

		/// <inheritdoc cref="IStream.Config"/>
		public StreamConfig? Config { get; }

		/// <inheritdoc cref="IStream.Deleted"/>
		public bool Deleted => _stream.Deleted;

		/// <inheritdoc cref="IStream.Templates"/>
		public List<GetTemplateRefResponseV2> Templates { get; }

		/// <inheritdoc cref="IStream.PausedUntil"/>
		public DateTime? PausedUntil => _stream.PausedUntil;

		/// <inheritdoc cref="IStream.PauseComment"/>
		public string? PauseComment => _stream.PauseComment;

		internal GetStreamResponseV2(IStream stream, bool config, List<GetTemplateRefResponseV2> templates)
		{
			_stream = stream;
			Templates = templates;

			if (config)
			{
				Config = stream.Config;
			}
		}
	}

	/// <summary>
	/// Job template in a stream
	/// </summary>
	public class GetTemplateRefResponseV2
	{
		readonly ITemplateRef _templateRef;

		/// <inheritdoc cref="ITemplateRef.Id"/>
		public TemplateId Id => _templateRef.Id;

		/// <inheritdoc cref="ITemplateRef.Hash"/>
		public ContentHash Hash => _templateRef.Hash;

		/// <inheritdoc cref="ITemplateRef.Schedule"/>
		public GetTemplateScheduleResponseV2? Schedule { get; }

		/// <inheritdoc cref="ITemplateRef.StepStates"/>
		public List<GetTemplateStepResponseV2>? StepStates { get; }

		internal GetTemplateRefResponseV2(ITemplateRef templateRef, List<GetTemplateStepResponseV2>? stepStates)
		{
			_templateRef = templateRef;

			StepStates = stepStates;

			if (templateRef.Schedule != null)
			{
				Schedule = new GetTemplateScheduleResponseV2(templateRef.Schedule);
			}
		}
	}

	/// <summary>
	/// Schedule for a template
	/// </summary>
	public class GetTemplateScheduleResponseV2
	{
		readonly ITemplateSchedule _schedule;

		/// <inheritdoc cref="ITemplateSchedule.LastTriggerChange"/>
		public int LastTriggerChange => _schedule.LastTriggerChange;

		/// <inheritdoc cref="ITemplateSchedule.LastTriggerTimeUtc"/>
		public DateTime LastTriggerTimeUtc => _schedule.LastTriggerTimeUtc;

		/// <inheritdoc cref="ITemplateSchedule.LastTriggerTimeUtc"/>
		public IReadOnlyList<JobId> ActiveJobs => _schedule.ActiveJobs;

		internal GetTemplateScheduleResponseV2(ITemplateSchedule schedule)
		{
			_schedule = schedule;
		}
	}

	/// <summary>
	/// State information for a step in the stream
	/// </summary>
	public class GetTemplateStepResponseV2
	{
		readonly ITemplateStep _step;

		/// <inheritdoc cref="ITemplateStep.Name"/>
		public string Name => _step.Name;

		/// <inheritdoc cref="ITemplateStep.PausedByUserId"/>
		public GetThinUserInfoResponse PausedByUserInfo { get; }

		/// <inheritdoc cref="ITemplateStep.PauseTimeUtc"/>
		public DateTime PauseTimeUtc => _step.PauseTimeUtc;

		/// <summary>
		/// Constructor
		/// </summary>
		public GetTemplateStepResponseV2(ITemplateStep step, GetThinUserInfoResponse pausedByUserInfo)
		{
			_step = step;
			PausedByUserInfo = pausedByUserInfo;
		}
	}

	/// <summary>
	/// Information about a commit
	/// </summary>
	public class GetCommitResponse
	{
		/// <summary>
		/// The source changelist number
		/// </summary>
		public int Number { get; set; }

		/// <summary>
		/// Name of the user that authored this change [DEPRECATED]
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// Information about the user that authored this change
		/// </summary>
		public GetThinUserInfoResponse AuthorInfo { get; set; }

		/// <summary>
		/// The description text
		/// </summary>
		public string Description { get; set; }

		/// <summary>
		/// Tags for this commit
		/// </summary>
		public List<CommitTag>? Tags { get; set; }

		/// <summary>
		/// List of files that were modified, relative to the stream base
		/// </summary>
		public List<string>? Files { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="commit">The commit to construct from</param>
		/// <param name="author">Author of the change</param>
		/// <param name="tags">Tags for the commit</param>
		/// <param name="files">Files modified by the commit</param>
		public GetCommitResponse(ICommit commit, IUser author, IReadOnlyList<CommitTag>? tags, IReadOnlyList<string>? files)
		{
			Number = commit.Number;
			Author = author.Name;
			AuthorInfo = new GetThinUserInfoResponse(author);
			Description = commit.Description;

			if (tags != null)
			{
				Tags = new List<CommitTag>(tags);
			}

			if (files != null)
			{
				Files = new List<string>(files);
			}
		}
	}
}
