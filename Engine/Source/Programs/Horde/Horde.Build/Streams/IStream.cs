// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Horde.Build.Acls;
using Horde.Build.Agents;
using Horde.Build.Agents.Pools;
using Horde.Build.Jobs.Schedules;
using Horde.Build.Jobs.Templates;
using Horde.Build.Projects;
using Horde.Build.Users;
using Horde.Build.Utilities;
using HordeCommon;
using MongoDB.Bson.Serialization.Attributes;

namespace Horde.Build.Streams
{
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

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
		public InvalidStreamException(string message) : base(message)
		{
		}

		/// <inheritdoc/>
		public InvalidStreamException(string message, Exception innerEx) : base(message, innerEx)
		{
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
		/// <param name="trigger">Name of the target that needs to complete</param>
		/// <param name="templateRefId">The new template to trigger</param>
		public ChainedJobTemplate(string trigger, TemplateRefId templateRefId)
		{
			Trigger = trigger;
			TemplateRefId = templateRefId;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="request">Request to construct from</param>
		public ChainedJobTemplate(ChainedJobTemplateConfig request)
			: this(request.Trigger, new TemplateRefId(request.TemplateId))
		{
		}
	}

	/// <summary>
	/// A
	/// </summary>
	public class TemplateStepState
	{
		/// <summary>
		/// Name of the step
		/// </summary>
		public string Name { get; set; } = String.Empty ;

		/// <summary>
		/// User who paused the step
		/// </summary>
		public UserId? PausedByUserId { get; set; }

		/// <summary>
		/// The UTC time when the step was paused
		/// </summary>
		public DateTime? PauseTimeUtc { get; set; }

		/// <summary>
		/// Default constructor for serialization
		/// </summary>
		private TemplateStepState()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public TemplateStepState( string name, UserId? pausedByUserId = null, DateTime? pauseTimeUtc = null)
		{
			Name = name;
			PausedByUserId = pausedByUserId;
			PauseTimeUtc = pauseTimeUtc;
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
		/// List of template step states
		/// </summary>
		[BsonIgnoreIfNull]
		public List<TemplateStepState>? StepStates { get;set; }

		/// <summary>
		/// Custom permissions for this template
		/// </summary>
		public Acl? Acl { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private TemplateRef()
		{
			Name = null!;
			Hash = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="template">The template being referenced</param>
		/// <param name="showUgsBadges">Whether to show badges in UGS for this job</param>
		/// <param name="showUgsAlerts">Whether to show alerts in UGS for this job</param>
		/// <param name="notificationChannel">Notification channel for this template</param>
		/// <param name="notificationChannelFilter">Notification channel filter for this template</param>
		/// <param name="triageChannel"></param>
		/// <param name="schedule">Schedule for this template</param>
		/// <param name="triggers">List of downstream templates to trigger</param>
		/// <param name="stepStates">List of template step states</param>
		/// <param name="acl">ACL for this template</param>
		public TemplateRef(ITemplate template, bool showUgsBadges = false, bool showUgsAlerts = false, string? notificationChannel = null, string? notificationChannelFilter = null, string? triageChannel = null, Schedule? schedule = null, List<ChainedJobTemplate>? triggers = null, List<TemplateStepState>? stepStates = null, Acl? acl = null)
		{
			Name = template.Name;
			Hash = template.Id;
			ShowUgsBadges = showUgsBadges;
			ShowUgsAlerts = showUgsAlerts;
			NotificationChannel = notificationChannel;
			NotificationChannelFilter = notificationChannelFilter;
			TriageChannel = triageChannel;
			Schedule = schedule;
			ChainedJobs = triggers;
			StepStates = stepStates;
			Acl = acl;
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
		public ChangeQueryConfig ToRequest()
		{
			return new ChangeQueryConfig { TemplateId = TemplateRefId?.ToString(), Target = Target, Outcomes = Outcomes };
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
		/// <param name="templateRefId"></param>
		/// <param name="change">The job type to query for the change to use</param>
		public DefaultPreflight(TemplateRefId? templateRefId, ChangeQuery? change)
		{
			TemplateRefId = templateRefId;
			Change = change;
		}

		/// <summary>
		/// Convert to a request object
		/// </summary>
		/// <returns></returns>
		public DefaultPreflightConfig ToRequest()
		{
#pragma warning disable CS0618 // Type or member is obsolete
			ChangeQueryConfig? changeRequest = null;
			if (Change != null)
			{
				changeRequest = Change.ToRequest();
			}
			else if (ChangeTemplateRefId != null)
			{
				changeRequest = new ChangeQueryConfig { TemplateId = ChangeTemplateRefId.ToString() };
			}

			return new DefaultPreflightConfig { TemplateId = TemplateRefId?.ToString(), Change = changeRequest, ChangeTemplateId = changeRequest?.TemplateId };
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
		/// <param name="templateRefs">List of template refs</param>
		/// <param name="templateRef">The template ref to add</param>
		public static void AddRef(this Dictionary<TemplateRefId, TemplateRef> templateRefs, TemplateRef templateRef)
		{
			templateRefs.Add(new TemplateRefId(templateRef.Name), templateRef);
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
		/// The revision of config file used for this stream
		/// </summary>
		public string ConfigRevision { get; }

		/// <summary>
		/// Configuration settings for the stream
		/// </summary>
		public StreamConfig Config { get; }

		/// <summary>
		/// Whether this stream has been deleted
		/// </summary>
		public bool Deleted { get; }

		/// <summary>
		/// Default template to use for preflights
		/// </summary>
		public DefaultPreflight? DefaultPreflight { get; }

		/// <summary>
		/// List of pages to display in the dashboard
		/// </summary>
		public IReadOnlyList<StreamTab> Tabs { get; }

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
		/// <param name="stream">The stream object</param>
		/// <param name="agentType">The agent type</param>
		/// <param name="workspace">Receives the agent workspace definition</param>
		/// <returns>True if the agent type was valid, and an agent workspace could be created</returns>
		public static bool TryGetAgentWorkspace(this IStream stream, AgentConfig agentType, [NotNullWhen(true)] out (AgentWorkspace, bool)? workspace)
		{
			// Get the workspace settings
			if (agentType.Workspace == null)
			{
				// Use the default settings (fast switching workspace, clean 
				workspace = (new AgentWorkspace(null, null, GetDefaultWorkspaceIdentifier(stream), stream.Name, null, false), true);
				return true;
			}
			else
			{
				// Try to get the matching workspace type
				WorkspaceConfig? workspaceType;
				if (!stream.Config.WorkspaceTypes.TryGetValue(agentType.Workspace, out workspaceType))
				{
					workspace = null;
					return false;
				}

				// Get the workspace identifier
				string identifier;
				if (workspaceType.Identifier != null)
				{
					identifier = workspaceType.Identifier;
				}
				else if (workspaceType.Incremental)
				{
					identifier = $"{stream.GetEscapedName()}+{agentType.Workspace}";
				}
				else
				{
					identifier = GetDefaultWorkspaceIdentifier(stream);
				}

				// Create the new workspace
				workspace = (new AgentWorkspace(workspaceType.Cluster, workspaceType.UserName, identifier, workspaceType.Stream ?? stream.Name, workspaceType.View, workspaceType.Incremental), workspaceType.UseAutoSdk);
				return true;
			}
		}

		/// <summary>
		/// The escaped name of this stream. Removes all non-identifier characters.
		/// </summary>
		/// <param name="stream">The stream object</param>
		/// <returns>Escaped name for the stream</returns>
		public static string GetEscapedName(this IStream stream)
		{
			return Regex.Replace(stream.Name, @"[^a-zA-Z0-9_]", "+");
		}

		/// <summary>
		/// Gets the default identifier for workspaces created for this stream. Just includes an escaped depot name.
		/// </summary>
		/// <param name="stream">The stream object</param>
		/// <returns>The default workspace identifier</returns>
		private static string GetDefaultWorkspaceIdentifier(IStream stream)
		{
			return Regex.Replace(stream.GetEscapedName(), @"^(\+\+[^+]*).*$", "$1");
		}

		/// <summary>
		/// Converts to a public response object
		/// </summary>
		/// <param name="stream">The stream object</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response object</param>
		/// <param name="apiTemplateRefs">The template refs for this stream. Passed separately because they have their own ACL.</param>
		/// <returns>New response instance</returns>
		public static GetStreamResponse ToApiResponse(this IStream stream, bool bIncludeAcl, List<GetTemplateRefResponse> apiTemplateRefs)
		{
			List<GetStreamTabResponse> apiTabs = stream.Tabs.ConvertAll(x => x.ToResponse());
			Dictionary<string, AgentConfig> apiAgentTypes = stream.Config.AgentTypes.ToDictionary(x => x.Key, x => x.Value);
			Dictionary<string, WorkspaceConfig> apiWorkspaceTypes = stream.Config.WorkspaceTypes.ToDictionary(x => x.Key, x => x.Value);
			GetAclResponse? apiAcl = (bIncludeAcl && stream.Acl != null)? new GetAclResponse(stream.Acl) : null;
			return new GetStreamResponse(stream.Id.ToString(), stream.ProjectId.ToString(), stream.Name, stream.ConfigRevision, stream.Config.Order, stream.Config.NotificationChannel, stream.Config.NotificationChannelFilter, stream.Config.TriageChannel, stream.DefaultPreflight?.ToRequest(), apiTabs, apiAgentTypes, apiWorkspaceTypes, apiTemplateRefs, apiAcl, stream.PausedUntil, stream.PauseComment, stream.Config.Workflows);
		}

		/// <summary>
		/// Converts to an RPC response object
		/// </summary>
		/// <param name="stream">The stream object</param>
		/// <returns>New response instance</returns>
		public static HordeCommon.Rpc.GetStreamResponse ToRpcResponse(this IStream stream)
		{
			HordeCommon.Rpc.GetStreamResponse response = new HordeCommon.Rpc.GetStreamResponse();
			response.Name = stream.Name;
			response.AgentTypes.Add(stream.Config.AgentTypes.ToDictionary(x => x.Key, x => x.Value.ToRpcResponse()));
			return response;
		}

		/// <summary>
		/// Check if stream is paused for new builds
		/// </summary>
		/// <param name="stream">The stream object</param>
		/// <param name="currentTime">Current time (allow tests to pass in a fake clock)</param>
		/// <returns>If stream is paused</returns>
		public static bool IsPaused(this IStream stream, DateTime currentTime)
		{
			return stream.PausedUntil != null && stream.PausedUntil > currentTime;
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
