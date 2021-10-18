// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Google.Protobuf.WellKnownTypes;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Runtime.CompilerServices;

namespace HordeServer.Models
{
	using PoolId = StringId<IPool>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Exception thrown when stream validation fails
	/// </summary>
	public class InvalidStreamException : Exception
	{
		/// <inheritdoc/>
		public InvalidStreamException()
		{
		}

		/// <inheritdoc/>
		public InvalidStreamException(string Message) : base(Message)
		{
		}

		/// <inheritdoc/>
		public InvalidStreamException(string Message, Exception InnerEx) : base(Message, InnerEx)
		{
		}
	}

	/// <summary>
	/// Mapping from a BuildGraph agent type to a set of machines on the farm
	/// </summary>
	public class AgentType
	{
		/// <summary>
		/// Name of the pool of agents to use
		/// </summary>
		[BsonRequired]
		public PoolId Pool { get; set; }

		/// <summary>
		/// Name of the workspace to execute on
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
		/// Default constructor
		/// </summary>
		[BsonConstructor]
		private AgentType()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pool">The pool for this agent</param>
		/// <param name="Workspace">Name of the workspace to use</param>
		/// <param name="TempStorageDir">Path to the temp storage directory</param>
		public AgentType(PoolId Pool, string Workspace, string? TempStorageDir)
		{
			this.Pool = Pool;
			this.Workspace = Workspace;
			this.TempStorageDir = TempStorageDir;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Request">The object to construct from</param>
		public AgentType(CreateAgentTypeRequest Request)
		{
			Pool = new PoolId(Request.Pool);
			Workspace = Request.Workspace;
			TempStorageDir = Request.TempStorageDir;
			Environment = Request.Environment;
		}

		/// <summary>
		/// Constructs an AgentType object from an optional request
		/// </summary>
		/// <param name="Request">The request object</param>
		/// <returns>New agent type object</returns>
		[return: NotNullIfNotNull("Request")]
		public static AgentType? FromRequest(CreateAgentTypeRequest? Request)
		{
			return (Request != null) ? new AgentType(Request) : null;
		}

		/// <summary>
		/// Creates an API response object from this stream
		/// </summary>
		/// <returns>The response object</returns>
		public Api.GetAgentTypeResponse ToApiResponse()
		{
			return new Api.GetAgentTypeResponse(Pool.ToString(), Workspace, TempStorageDir, Environment);
		}

		/// <summary>
		/// Creates an API response object from this stream
		/// </summary>
		/// <returns>The response object</returns>
		public HordeCommon.Rpc.GetAgentTypeResponse ToRpcResponse()
		{
			HordeCommon.Rpc.GetAgentTypeResponse Response = new HordeCommon.Rpc.GetAgentTypeResponse();
			if (TempStorageDir != null)
			{
				Response.TempStorageDir = TempStorageDir;
			}
			if (Environment != null)
			{
				Response.Environment.Add(Environment);
			}
			return Response;
		}
	}

	/// <summary>
	/// Information about a workspace type
	/// </summary>
	public class WorkspaceType
	{
		/// <summary>
		/// Name of the Perforce cluster to use
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Cluster { get; set; }

		/// <summary>
		/// The Perforce server and port
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ServerAndPort { get; set; }

		/// <summary>
		/// The Perforce username for syncing this workspace
		/// </summary>
		[BsonIgnoreIfNull]
		public string? UserName { get; set; }

		/// <summary>
		/// The Perforce password for syncing this workspace
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Password { get; set; }

		/// <summary>
		/// Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Identifier { get; set; }

		/// <summary>
		/// Override for the stream to sync
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Stream { get; set; }

		/// <summary>
		/// Custom view for the workspace
		/// </summary>
		[BsonIgnoreIfNull]
		public List<string>? View { get; set; }

		/// <summary>
		/// Whether to use an incrementally synced workspace
		/// </summary>
		public bool Incremental { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public WorkspaceType()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Request">The object to construct from</param>
		public WorkspaceType(CreateWorkspaceTypeRequest Request)
		{
			Cluster = Request.Cluster;
			ServerAndPort = Request.ServerAndPort;
			UserName = Request.UserName;
			Password = Request.Password;
			Identifier = Request.Identifier;
			Stream = Request.Stream;
			View = Request.View;
			Incremental = Request.Incremental;
		}

		/// <summary>
		/// Constructs an AgentType object from an optional request
		/// </summary>
		/// <param name="Request">The request object</param>
		/// <returns>New agent type object</returns>
		[return: NotNullIfNotNull("Request")]
		public static WorkspaceType? FromRequest(CreateWorkspaceTypeRequest? Request)
		{
			return (Request != null) ? new WorkspaceType(Request) : null;
		}

		/// <summary>
		/// Creates an API response object from this stream
		/// </summary>
		/// <returns>The response object</returns>
		public Api.GetWorkspaceTypeResponse ToApiResponse()
		{
			return new Api.GetWorkspaceTypeResponse(Cluster, ServerAndPort, UserName, Identifier, Stream, View, Incremental);
		}
	}

	/// <summary>
	/// Allows triggering another downstream job on succesful completion of a step or aggregate
	/// </summary>
	public class ChainedJobTemplate
	{
		/// <summary>
		/// Name of the target that needs to complete successfully
		/// </summary>
		public string Trigger { get; set; } = String.Empty;

		/// <summary>
		/// The new template to trigger
		/// </summary>
		public TemplateRefId TemplateRefId { get; set; }

		/// <summary>
		/// Default constructor for serialization
		/// </summary>
		private ChainedJobTemplate()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Trigger">Name of the target that needs to complete</param>
		/// <param name="TemplateRefId">The new template to trigger</param>
		public ChainedJobTemplate(string Trigger, TemplateRefId TemplateRefId)
		{
			this.Trigger = Trigger;
			this.TemplateRefId = TemplateRefId;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Request">Request to construct from</param>
		public ChainedJobTemplate(CreateChainedJobTemplateRequest Request)
			: this(Request.Trigger, new TemplateRefId(Request.TemplateId))
		{
		}
	}

	/// <summary>
	/// Reference to a template
	/// </summary>
	public class TemplateRef
	{
		/// <summary>
		/// The template name (duplicated from the template object)
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Hash of the template definition
		/// </summary>
		public ContentHash Hash { get; set; }

		/// <summary>
		/// Whether to show badges in UGS for this schedule
		/// </summary>
		public bool ShowUgsBadges { get; set; }

		/// <summary>
		/// Whether to show desktop alerts for build health issues created from jobs this type
		/// </summary>
		public bool ShowUgsAlerts { get; set; }

		/// <summary>
		/// Notification channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Errors|Warnings|Success
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Channel for triage notification messages
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// List of schedules for this template
		/// </summary>
		[BsonIgnoreIfNull]
		public Schedule? Schedule { get; set; }

		/// <summary>
		/// List of downstream templates to trigger at the same change
		/// </summary>
		[BsonIgnoreIfNull]
		public List<ChainedJobTemplate>? ChainedJobs { get; set; }

		/// <summary>
		/// Custom permissions for this template
		/// </summary>
		public Acl? Acl { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private TemplateRef()
		{
			this.Name = null!;
			this.Hash = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Template">The template being referenced</param>
		/// <param name="ShowUgsBadges">Whether to show badges in UGS for this job</param>
		/// <param name="ShowUgsAlerts">Whether to show alerts in UGS for this job</param>
		/// <param name="NotificationChannel">Notification channel for this template</param>
		/// <param name="NotificationChannelFilter">Notification channel filter for this template</param>
		/// <param name="TriageChannel"></param>
		/// <param name="Schedule">Schedule for this template</param>
		/// <param name="Triggers">List of downstream templates to trigger</param>
		/// <param name="Acl">ACL for this template</param>
		public TemplateRef(ITemplate Template, bool ShowUgsBadges = false, bool ShowUgsAlerts = false, string? NotificationChannel = null, string? NotificationChannelFilter = null, string? TriageChannel = null, Schedule? Schedule = null, List<ChainedJobTemplate>? Triggers = null, Acl? Acl = null)
		{
			this.Name = Template.Name;
			this.Hash = Template.Id;
			this.ShowUgsBadges = ShowUgsBadges;
			this.ShowUgsAlerts = ShowUgsAlerts;
			this.NotificationChannel = NotificationChannel;
			this.NotificationChannelFilter = NotificationChannelFilter;
			this.TriageChannel = TriageChannel;
			this.Schedule = Schedule;
			this.ChainedJobs = Triggers;
			this.Acl = Acl;
		}
	}

	/// <summary>
	/// Query used to identify a base changelist for a preflight
	/// </summary>
	public class ChangeQuery
	{
		/// <summary>
		/// Template to search for
		/// </summary>
		public TemplateRefId? TemplateRefId { get; set; }

		/// <summary>
		/// The target to look at the status for
		/// </summary>
		public string? Target { get; set; }

		/// <summary>
		/// Whether to match a job that contains warnings
		/// </summary>
		public List<JobStepOutcome>? Outcomes { get; set; }

		/// <summary>
		/// Convert to a request object
		/// </summary>
		/// <returns></returns>
		public ChangeQueryRequest ToRequest()
		{
			return new ChangeQueryRequest { TemplateId = TemplateRefId?.ToString(), Target = Target, Outcomes = Outcomes };
		}
	}

	/// <summary>
	/// Definition of a query to execute to find the changelist to run a build at
	/// </summary>
	public class DefaultPreflight
	{
		/// <summary>
		/// The template id to execute
		/// </summary>
		public TemplateRefId? TemplateRefId { get; set; }

		/// <summary>
		/// Query specifying a changelist to use
		/// </summary>
		public ChangeQuery? Change { get; set; }

		/// <summary>
		/// The job type to query for the change to use
		/// </summary>
		[Obsolete("Use Change.TemplateRefId instead")]
		public TemplateRefId? ChangeTemplateRefId { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TemplateRefId"></param>
		/// <param name="Change">The job type to query for the change to use</param>
		public DefaultPreflight(TemplateRefId? TemplateRefId, ChangeQuery? Change)
		{
			this.TemplateRefId = TemplateRefId;
			this.Change = Change;
		}

		/// <summary>
		/// Convert to a request object
		/// </summary>
		/// <returns></returns>
		public DefaultPreflightRequest ToRequest()
		{
#pragma warning disable CS0618 // Type or member is obsolete
			ChangeQueryRequest? ChangeRequest = null;
			if (Change != null)
			{
				ChangeRequest = Change.ToRequest();
			}
			else if (ChangeTemplateRefId != null)
			{
				ChangeRequest = new ChangeQueryRequest { TemplateId = ChangeTemplateRefId?.ToString() };
			}

			return new DefaultPreflightRequest { TemplateId = TemplateRefId?.ToString(), Change = ChangeRequest, ChangeTemplateId = ChangeRequest?.TemplateId };
#pragma warning restore CS0618 // Type or member is obsolete
		}
	}

	/// <summary>
	/// Extension methods for template refs
	/// </summary>
	static class TemplateRefExtensions
	{
		/// <summary>
		/// Adds a new template ref to a list
		/// </summary>
		/// <param name="TemplateRefs">List of template refs</param>
		/// <param name="TemplateRef">The template ref to add</param>
		public static void AddRef(this Dictionary<TemplateRefId, TemplateRef> TemplateRefs, TemplateRef TemplateRef)
		{
			TemplateRefs.Add(new TemplateRefId(TemplateRef.Name), TemplateRef);
		}
	}

	/// <summary>
	/// Information about a stream
	/// </summary>
	public interface IStream
	{
		/// <summary>
		/// Name of the stream.
		/// </summary>
		public StreamId Id { get; }

		/// <summary>
		/// The project that this stream belongs to
		/// </summary>
		public ProjectId ProjectId { get; }

		/// <summary>
		/// The stream name
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Name of the perforce cluster
		/// </summary>
		public string ClusterName { get; }

		/// <summary>
		/// Path to the configuration file for this stream
		/// </summary>
		public string ConfigPath { get; }

		/// <summary>
		/// The revision of config file used for this stream
		/// </summary>
		public string ConfigRevision { get; }

		/// <summary>
		/// Order to display on the dashboard's drop-down list
		/// </summary>
		public int Order { get; }

		/// <summary>
		/// Notification channel for all jobs in this stream
		/// </summary>
		public string? NotificationChannel { get; }

		/// <summary>
		/// Notification channel filter for all jobs in this stream. Errors|Warnings|Success
		/// </summary>
		public string? NotificationChannelFilter { get; }

		/// <summary>
		/// Channel to post issue triage notifications
		/// </summary>
		public string? TriageChannel { get; }

		/// <summary>
		/// Default template to use for preflights
		/// </summary>
		public DefaultPreflight? DefaultPreflight { get; }

		/// <summary>
		/// List of pages to display in the dashboard
		/// </summary>
		public IReadOnlyList<StreamTab> Tabs { get; }

		/// <summary>
		/// Dictionary of agent types
		/// </summary>
		public IReadOnlyDictionary<string, AgentType> AgentTypes { get; }

		/// <summary>
		/// Dictionary of workspace types
		/// </summary>
		public IReadOnlyDictionary<string, WorkspaceType> WorkspaceTypes { get; }

		/// <summary>
		/// List of templates available for this stream
		/// </summary>
		public IReadOnlyDictionary<TemplateRefId, TemplateRef> Templates { get; }

		/// <summary>
		/// Stream is paused for builds until specified time
		/// </summary>
		public DateTime? PausedUntil { get; }
		
		/// <summary>
		/// Comment/reason for why the stream was paused
		/// </summary>
		public string? PauseComment { get; }

		/// <summary>
		/// The ACL for this object
		/// </summary>
		public Acl? Acl { get; }
	}

	/// <summary>
	/// Extension methods for streams
	/// </summary>
	static class StreamExtensions
	{
		/// <summary>
		/// Tries to get an agent workspace definition from the given type name
		/// </summary>
		/// <param name="Stream">The stream object</param>
		/// <param name="AgentType">The agent type</param>
		/// <param name="Workspace">Receives the agent workspace definition</param>
		/// <returns>True if the agent type was valid, and an agent workspace could be created</returns>
		public static bool TryGetAgentWorkspace(this IStream Stream, AgentType AgentType, [NotNullWhen(true)] out AgentWorkspace? Workspace)
		{
			// Get the workspace settings
			if (AgentType.Workspace == null)
			{
				// Use the default settings (fast switching workspace, clean 
				Workspace = new AgentWorkspace(null, null, GetDefaultWorkspaceIdentifier(Stream), Stream.Name, null, false);
				return true;
			}
			else
			{
				// Try to get the matching workspace type
				WorkspaceType? WorkspaceType;
				if (!Stream.WorkspaceTypes.TryGetValue(AgentType.Workspace, out WorkspaceType))
				{
					Workspace = null;
					return false;
				}

				// Get the workspace identifier
				string Identifier;
				if (WorkspaceType.Identifier != null && !String.IsNullOrEmpty(WorkspaceType.Identifier))
				{
					Identifier = WorkspaceType.Identifier;
				}
				else if (WorkspaceType.Incremental)
				{
					Identifier = $"{Stream.GetEscapedName()}+{AgentType.Workspace}";
				}
				else
				{
					Identifier = GetDefaultWorkspaceIdentifier(Stream);
				}

				// Create the new workspace
				Workspace = new AgentWorkspace(WorkspaceType.Cluster, WorkspaceType.UserName, Identifier, WorkspaceType.Stream ?? Stream.Name, WorkspaceType.View, WorkspaceType.Incremental);
				return true;
			}
		}

		/// <summary>
		/// The escaped name of this stream. Removes all non-identifier characters.
		/// </summary>
		/// <param name="Stream">The stream object</param>
		/// <returns>Escaped name for the stream</returns>
		public static string GetEscapedName(this IStream Stream)
		{
			return Regex.Replace(Stream.Name, @"[^a-zA-Z0-9_]", "+");
		}

		/// <summary>
		/// Gets the default identifier for workspaces created for this stream. Just includes an escaped depot name.
		/// </summary>
		/// <param name="Stream">The stream object</param>
		/// <returns>The default workspace identifier</returns>
		private static string GetDefaultWorkspaceIdentifier(IStream Stream)
		{
			return Regex.Replace(Stream.GetEscapedName(), @"^(\+\+[^+]*).*$", "$1");
		}

		/// <summary>
		/// Converts to a public response object
		/// </summary>
		/// <param name="Stream">The stream object</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response object</param>
		/// <param name="ApiTemplateRefs">The template refs for this stream. Passed separately because they have their own ACL.</param>
		/// <returns>New response instance</returns>
		public static Api.GetStreamResponse ToApiResponse(this IStream Stream, bool bIncludeAcl, List<GetTemplateRefResponse> ApiTemplateRefs)
		{
			List<GetStreamTabResponse> ApiTabs = Stream.Tabs.ConvertAll(x => x.ToResponse());
			Dictionary<string, GetAgentTypeResponse> ApiAgentTypes = Stream.AgentTypes.ToDictionary(x => x.Key, x => x.Value.ToApiResponse());
			Dictionary<string, GetWorkspaceTypeResponse> ApiWorkspaceTypes = Stream.WorkspaceTypes.ToDictionary(x => x.Key, x => x.Value.ToApiResponse());
			GetAclResponse? ApiAcl = (bIncludeAcl && Stream.Acl != null)? new GetAclResponse(Stream.Acl) : null;
			return new Api.GetStreamResponse(Stream.Id.ToString(), Stream.ProjectId.ToString(), Stream.Name, Stream.ConfigPath, Stream.ConfigRevision, Stream.Order, Stream.NotificationChannel, Stream.NotificationChannelFilter, Stream.TriageChannel, Stream.DefaultPreflight?.ToRequest(), ApiTabs, ApiAgentTypes, ApiWorkspaceTypes, ApiTemplateRefs, ApiAcl, Stream.PausedUntil, Stream.PauseComment);
		}

		/// <summary>
		/// Converts to an RPC response object
		/// </summary>
		/// <param name="Stream">The stream object</param>
		/// <returns>New response instance</returns>
		public static HordeCommon.Rpc.GetStreamResponse ToRpcResponse(this IStream Stream)
		{
			HordeCommon.Rpc.GetStreamResponse Response = new HordeCommon.Rpc.GetStreamResponse();
			Response.Name = Stream.Name;
			Response.AgentTypes.Add(Stream.AgentTypes.ToDictionary(x => x.Key, x => x.Value.ToRpcResponse()));
			return Response;
		}

		/// <summary>
		/// Check if stream is paused for new builds
		/// </summary>
		/// <param name="Stream">The stream object</param>
		/// <param name="CurrentTime">Current time (allow tests to pass in a fake clock)</param>
		/// <returns>If stream is paused</returns>
		public static bool IsPaused(this IStream Stream, DateTime CurrentTime)
		{
			return Stream.PausedUntil != null && Stream.PausedUntil > CurrentTime;
		}
	}

	/// <summary>
	/// Projection of a stream definition to just include permissions info
	/// </summary>
	public interface IStreamPermissions
	{
		/// <summary>
		/// ACL for the stream
		/// </summary>
		public Acl? Acl { get; }

		/// <summary>
		/// The project containing this stream
		/// </summary>
		public ProjectId ProjectId { get; }
	}
}
