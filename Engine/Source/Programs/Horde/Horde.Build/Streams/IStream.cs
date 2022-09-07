// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Horde.Build.Acls;
using Horde.Build.Agents;
using Horde.Build.Jobs;
using Horde.Build.Perforce;
using Horde.Build.Projects;
using Horde.Build.Users;
using Horde.Build.Utilities;

namespace Horde.Build.Streams
{
	using JobId = ObjectId<IJob>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateId = StringId<ITemplateRef>;
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
	/// Information about a stream
	/// </summary>
	public interface IStream
	{
		/// <summary>
		/// Name of the stream.
		/// </summary>
		StreamId Id { get; }

		/// <summary>
		/// The project that this stream belongs to
		/// </summary>
		ProjectId ProjectId { get; }

		/// <summary>
		/// The stream name
		/// </summary>
		string Name { get; }

		/// <summary>
		/// The revision of config file used for this stream
		/// </summary>
		string ConfigRevision { get; }

		/// <summary>
		/// Configuration settings for the stream
		/// </summary>
		StreamConfig Config { get; }

		/// <summary>
		/// Whether this stream has been deleted
		/// </summary>
		bool Deleted { get; }

		/// <summary>
		/// List of templates available for this stream
		/// </summary>
		IReadOnlyDictionary<TemplateId, ITemplateRef> Templates { get; }

		/// <summary>
		/// Stream is paused for builds until specified time
		/// </summary>
		DateTime? PausedUntil { get; }
		
		/// <summary>
		/// Comment/reason for why the stream was paused
		/// </summary>
		string? PauseComment { get; }

		/// <summary>
		/// The ACL for this object
		/// </summary>
		Acl? Acl { get; }

		/// <summary>
		/// Accessor for commits of this stream
		/// </summary>
		ICommitSource Commits { get; }
	}

	/// <summary>
	/// Job template in a stream
	/// </summary>
	public interface ITemplateRef
	{
		/// <summary>
		/// The template id
		/// </summary>
		TemplateId Id { get; }

		/// <summary>
		/// Configuration of this template ref
		/// </summary>
		TemplateRefConfig Config { get; }

		/// <summary>
		/// Hash of the template definition
		/// </summary>
		ContentHash Hash { get; }

		/// <summary>
		/// List of schedules for this template
		/// </summary>
		ITemplateSchedule? Schedule { get; }

		/// <summary>
		/// List of template step states
		/// </summary>
		IReadOnlyList<ITemplateStep> StepStates { get; }

		/// <summary>
		/// Custom permissions for this template
		/// </summary>
		Acl? Acl { get; set; }
	}

	/// <summary>
	/// Schedule for a template
	/// </summary>
	public interface ITemplateSchedule
	{
		/// <summary>
		/// Config for this schedule
		/// </summary>
		ScheduleConfig Config { get; }

		/// <summary>
		/// Last changelist number that this was triggered for
		/// </summary>
		int LastTriggerChange { get; }

		/// <summary>
		/// Gets the last trigger time, in UTC
		/// </summary>
		DateTime LastTriggerTimeUtc { get; }

		/// <summary>
		/// List of jobs that are currently active
		/// </summary>
		IReadOnlyList<JobId> ActiveJobs { get; }
	}

	/// <summary>
	/// Information about a paused template step
	/// </summary>
	public interface ITemplateStep
	{
		/// <summary>
		/// Name of the step
		/// </summary>
		string Name { get; }

		/// <summary>
		/// User who paused the step
		/// </summary>
		UserId PausedByUserId { get; }

		/// <summary>
		/// The UTC time when the step was paused
		/// </summary>
		DateTime PauseTimeUtc { get; }
	}

	/// <summary>
	/// Extension methods for streams
	/// </summary>
	static class StreamExtensions
	{
		/// <summary>
		/// Gets the next trigger time for a schedule
		/// </summary>
		/// <param name="schedule"></param>
		/// <param name="timeZone"></param>
		/// <returns></returns>
		public static DateTime? GetNextTriggerTimeUtc(this ITemplateSchedule schedule, TimeZoneInfo timeZone)
		{
			return schedule.Config.GetNextTriggerTimeUtc(schedule.LastTriggerTimeUtc, timeZone);
		}

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
			List<TabConfig> apiTabs = stream.Config.Tabs;
			Dictionary<string, AgentConfig> apiAgentTypes = stream.Config.AgentTypes.ToDictionary(x => x.Key, x => x.Value);
			Dictionary<string, WorkspaceConfig> apiWorkspaceTypes = stream.Config.WorkspaceTypes.ToDictionary(x => x.Key, x => x.Value);
			GetAclResponse? apiAcl = (bIncludeAcl && stream.Acl != null)? new GetAclResponse(stream.Acl) : null;
			return new GetStreamResponse(stream.Id.ToString(), stream.ProjectId.ToString(), stream.Name, stream.ConfigRevision, stream.Config.Order, stream.Config.NotificationChannel, stream.Config.NotificationChannelFilter, stream.Config.TriageChannel, stream.Config.DefaultPreflight, apiTabs, apiAgentTypes, apiWorkspaceTypes, apiTemplateRefs, apiAcl, stream.PausedUntil, stream.PauseComment, stream.Config.Workflows);
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
