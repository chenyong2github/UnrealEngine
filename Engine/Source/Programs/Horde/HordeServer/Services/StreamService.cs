// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Text;
using System.Threading.Tasks;
using System.Linq.Expressions;

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using HordeServer.Collections;
using Microsoft.Extensions.Caching.Memory;

namespace HordeServer.Services
{
	/// <summary>
	/// Cache of information about stream ACLs
	/// </summary>
	public class StreamPermissionsCache : ProjectPermissionsCache
	{
		/// <summary>
		/// Map of stream id to permissions for that stream
		/// </summary>
		public Dictionary<StreamId, IStreamPermissions?> Streams { get; set; } = new Dictionary<StreamId, IStreamPermissions?>();
	}

	/// <summary>
	/// Wraps functionality for manipulating streams
	/// </summary>
	public sealed class StreamService : IDisposable
	{
		/// <summary>
		/// The project service instance
		/// </summary>
		ProjectService ProjectService;

		/// <summary>
		/// Collection of stream documents
		/// </summary>
		IStreamCollection Streams;

		/// <summary>
		/// Cache of stream documents
		/// </summary>
		MemoryCache StreamCache = new MemoryCache(new MemoryCacheOptions());

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ProjectService">The project service instance</param>
		/// <param name="Streams">Collection of stream documents</param>
		public StreamService(ProjectService ProjectService, IStreamCollection Streams)
		{
			this.ProjectService = ProjectService;
			this.Streams = Streams;
		}

		/// <summary>
		/// Dispose of any managed resources
		/// </summary>
		public void Dispose()
		{
			StreamCache.Dispose();
		}

		/// <summary>
		/// Creates a new stream
		/// </summary>
		/// <param name="Id">Unique id for the new stream</param>
		/// <param name="Name">Name of the new stream</param>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <param name="ConfigPath">Path to the config file in Perforce</param>
		/// <param name="Order">Order for this stream</param>
		/// <param name="NotificationChannel">Notification channel for this stream</param>
		/// <param name="NotificationChannelFilter">Notification channel filter for this stream</param>
		/// <param name="TriageChannel"></param>
		/// <param name="DefaultPreflight">Default template for preflights</param>
		/// <param name="Tabs">Tabs for the new stream</param>
		/// <param name="AgentTypes">Map of new agent types to machine attributes</param>
		/// <param name="WorkspaceTypes">Map of new workspace types</param>
		/// <param name="TemplateRefs">New job templates for the stream</param>
		/// <param name="Properties">New properties for the stream</param>
		/// <param name="Acl">Acl for the stream</param>
		/// <returns>The new stream document</returns>
		public Task<IStream?> TryCreateStreamAsync(StreamId Id, string Name, ProjectId ProjectId, string? ConfigPath = null, int? Order = null, string? NotificationChannel = null, string? NotificationChannelFilter = null, string? TriageChannel = null, DefaultPreflight? DefaultPreflight = null, List<StreamTab>? Tabs = null, Dictionary<string, AgentType>? AgentTypes = null, Dictionary<string, WorkspaceType>? WorkspaceTypes = null, Dictionary<TemplateRefId, TemplateRef>? TemplateRefs = null, Dictionary<string, string>? Properties = null, Acl? Acl = null)
		{
			return Streams.TryCreateAsync(Id, Name, ProjectId, ConfigPath, Order, NotificationChannel, NotificationChannelFilter, TriageChannel, DefaultPreflight, Tabs, AgentTypes, WorkspaceTypes, TemplateRefs, Properties, Acl);
		}

		/// <summary>
		/// Replaces a stream
		/// </summary>
		/// <param name="Stream">The stream interface</param>
		/// <param name="Name">Name of the new stream</param>
		/// <param name="ConfigPath">Path to the config file in Perforce</param>
		/// <param name="ConfigChange">CL number of the config file</param>
		/// <param name="Order">Order for this stream</param>
		/// <param name="NotificationChannel">Notification channel for this stream</param>
		/// <param name="NotificationChannelFilter">Notification channel filter for this stream</param>
		/// <param name="TriageChannel"></param>
		/// <param name="DefaultPreflight">Default template for preflights</param>
		/// <param name="Tabs">Tabs for the new stream</param>
		/// <param name="AgentTypes">Map of new agent types to machine attributes</param>
		/// <param name="WorkspaceTypes">Map of new workspace types</param>
		/// <param name="TemplateRefs">New job templates for the stream</param>
		/// <param name="Properties">New properties for the stream</param>
		/// <param name="Acl">Acl for the stream</param>
		/// <param name="PausedUntil">The new datetime for pausing builds</param>
		/// <param name="PauseComment">The reason for pausing</param>
		/// <returns>The new stream document</returns>
		public Task<IStream?> TryReplaceStreamAsync(IStream Stream, string Name, string? ConfigPath = null, int? ConfigChange = null, int? Order = null, string? NotificationChannel = null, string? NotificationChannelFilter = null, string? TriageChannel = null, DefaultPreflight? DefaultPreflight = null, List<StreamTab>? Tabs = null, Dictionary<string, AgentType>? AgentTypes = null, Dictionary<string, WorkspaceType>? WorkspaceTypes = null, Dictionary<TemplateRefId, TemplateRef>? TemplateRefs = null, Dictionary<string, string>? Properties = null, Acl? Acl = null, DateTime? PausedUntil = null, string? PauseComment = null)
		{
			return Streams.TryReplaceAsync(Stream, Name, ConfigPath, ConfigChange, Order, NotificationChannel, NotificationChannelFilter, TriageChannel, DefaultPreflight, Tabs, AgentTypes, WorkspaceTypes, TemplateRefs, Properties, Acl, PausedUntil, PauseComment);
		}

		/// <summary>
		/// Deletes an existing stream
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <param name="JobService">Instance of the job service</param>
		/// <returns>Async task object</returns>
		public async Task DeleteStreamAsync(StreamId StreamId, JobService JobService)
		{
			await JobService.DeleteJobsForStreamAsync(StreamId);
			await Streams.DeleteAsync(StreamId);
		}

		/// <summary>
		/// Deletes all the streams for a particular project
		/// </summary>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <param name="JobService">Instance of the job service</param>
		/// <returns>Async task object</returns>
		public async Task DeleteStreamsForProjectAsync(ProjectId ProjectId, JobService JobService)
		{
			List<IStream> Streams = await GetStreamsAsync(new[] { ProjectId });
			foreach (IStream Stream in Streams)
			{
				await DeleteStreamAsync(Stream.Id, JobService);
			}
		}

		/// <summary>
		/// Updates an existing stream
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="NewName">The new name for the stream</param>
		/// <param name="NewOrder">The new order for the stream</param>
		/// <param name="NewNotificationChannel">The new notification channel for the stream</param>
		/// <param name="NewNotificationChannelFilter">The new notification channel filter for the stream</param>
		/// <param name="NewTriageChannel"></param>
		/// <param name="NewTabs">New tabs for the stream</param>
		/// <param name="NewAgentTypes">Map of agent types to update. Anything with a value of null will be removed.</param>
		/// <param name="NewWorkspaceTypes">Map of workspace types to update. Anything with a value of null will be removed.</param>
		/// <param name="NewTemplateRefs">New template references for this stream</param>
		/// <param name="NewProperties">Properties on the stream to update. Anything with a value of null will be removed.</param>
		/// <param name="NewAcl">The new ACL object</param>
		/// <param name="UpdatePauseFields">Must be set to true to update the pause fields</param>
		/// <param name="NewPausedUntil">The new datetime for pausing builds</param>
		/// <param name="NewPauseComment">The reason for pausing</param>
		/// <returns>Async task object</returns>
		public async Task<IStream?> UpdateStreamAsync(IStream? Stream, string? NewName = null, int? NewOrder = null, string? NewNotificationChannel = null, string? NewNotificationChannelFilter = null, string? NewTriageChannel = null, List<StreamTab>? NewTabs = null, Dictionary<string, AgentType?>? NewAgentTypes = null, Dictionary<string, WorkspaceType?>? NewWorkspaceTypes = null, Dictionary<TemplateRefId, TemplateRef>? NewTemplateRefs = null, Dictionary<string, string?>? NewProperties = null, Acl? NewAcl = null, bool? UpdatePauseFields = null, DateTime? NewPausedUntil = null, string? NewPauseComment = null)
		{
			for (; Stream != null; Stream = await GetStreamAsync(Stream.Id))
			{
				if (await TryUpdateStreamAsync(Stream, NewName, NewOrder, NewNotificationChannel, NewNotificationChannelFilter, NewTriageChannel, NewTabs, NewAgentTypes, NewWorkspaceTypes, NewTemplateRefs, NewProperties, NewAcl, UpdatePauseFields, NewPausedUntil, NewPauseComment))
				{
					break;
				}
			}
			return Stream;
		}

		/// <summary>
		/// Attempts to update the last trigger time for a schedule
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="TemplateRefId">The template ref id</param>
		/// <param name="LastTriggerTime"></param>
		/// <param name="LastTriggerChange"></param>
		/// <param name="AddJobs">Jobs to add</param>
		/// <param name="RemoveJobs">Jobs to remove</param>
		/// <returns>True if the stream was updated</returns>
		public async Task<IStream?> UpdateScheduleTriggerAsync(IStream Stream, TemplateRefId TemplateRefId, DateTimeOffset? LastTriggerTime, int? LastTriggerChange, List<ObjectId> AddJobs, List<ObjectId> RemoveJobs)
		{
			IStream? NewStream = Stream;
			while (NewStream != null)
			{
				TemplateRef? TemplateRef;
				if (!NewStream.Templates.TryGetValue(TemplateRefId, out TemplateRef))
				{
					break;
				}
				if (TemplateRef.Schedule == null)
				{
					break;
				}

				List<ObjectId> NewActiveJobs = TemplateRef.Schedule.ActiveJobs.Except(RemoveJobs).Union(AddJobs).ToList();
				if (await Streams.TryUpdateScheduleTriggerAsync(NewStream, TemplateRefId, LastTriggerTime, LastTriggerChange, NewActiveJobs))
				{
					return NewStream;
				}

				NewStream = await Streams.GetAsync(NewStream.Id);
			}
			return null;
		}

		/// <summary>
		/// Updates an existing stream
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="NewName">The new name for the stream</param>
		/// <param name="NewOrder">New order for the stream</param>
		/// <param name="NewNotificationChannel">New notification channel for the stream</param>
		/// <param name="NewNotificationChannelFilter">New notification channel filter for the stream</param>
		/// <param name="NewTriageChannel"></param>
		/// <param name="NewTabs">New tabs for the stream</param>
		/// <param name="NewAgentTypes">Map of agent types to update. Anything with a value of null will be removed.</param>
		/// <param name="NewWorkspaceTypes">Map of workspace types to update. Anything with a value of null will be removed.</param>
		/// <param name="NewTemplateRefs">New template references for this stream</param>
		/// <param name="NewProperties">Properties on the stream to update. Anything with a value of null will be removed.</param>
		/// <param name="NewAcl">The new ACL object</param>
		/// <param name="UpdatePauseFields">Must be set to true to update the pause fields</param>
		/// <param name="NewPausedUntil">The new datetime for pausing builds</param>
		/// <param name="NewPauseComment">The reason for pausing</param>
		/// <returns>Async task object</returns>
		public Task<bool> TryUpdateStreamAsync(IStream Stream, string? NewName, int? NewOrder, string? NewNotificationChannel, string? NewNotificationChannelFilter, string? NewTriageChannel, List<StreamTab>? NewTabs, Dictionary<string, AgentType?>? NewAgentTypes, Dictionary<string, WorkspaceType?>? NewWorkspaceTypes, Dictionary<TemplateRefId, TemplateRef>? NewTemplateRefs, Dictionary<string, string?>? NewProperties, Acl? NewAcl, bool? UpdatePauseFields = null, DateTime? NewPausedUntil = null, string? NewPauseComment = null)
		{
			return Streams.TryUpdatePropertiesAsync(Stream, NewName, NewOrder, NewNotificationChannel, NewNotificationChannelFilter, NewTriageChannel, NewTabs, NewAgentTypes, NewWorkspaceTypes, NewTemplateRefs, NewProperties, NewAcl, UpdatePauseFields, NewPausedUntil, NewPauseComment);
		}

		/// <summary>
		/// Gets all the available streams for a project
		/// </summary>
		/// <returns>List of stream documents</returns>
		public Task<List<IStream>> GetStreamsAsync()
		{
			return Streams.FindAllAsync();
		}

		/// <summary>
		/// Gets all the available streams for a project
		/// </summary>
		/// <param name="ProjectId">Unique id of the project to query</param>
		/// <returns>List of stream documents</returns>
		public Task<List<IStream>> GetStreamsAsync(ProjectId ProjectId)
		{
			return Streams.FindForProjectsAsync(new[] { ProjectId });
		}

		/// <summary>
		/// Gets all the available streams for a project
		/// </summary>
		/// <param name="ProjectIds">Unique id of the project to query</param>
		/// <returns>List of stream documents</returns>
		public Task<List<IStream>> GetStreamsAsync(ProjectId[] ProjectIds)
		{
			if (ProjectIds.Length == 0)
			{
				return Streams.FindAllAsync();
			}
			else
			{
				return Streams.FindForProjectsAsync(ProjectIds);
			}
		}

		/// <summary>
		/// Gets a stream by ID
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>The stream document</returns>
		public Task<IStream?> GetStreamAsync(StreamId StreamId)
		{
			return Streams.GetAsync(StreamId);
		}

		/// <summary>
		/// Adds a cached stream interface
		/// </summary>
		/// <param name="Stream">The stream to add</param>
		/// <returns>The new stream</returns>
		private void AddCachedStream(IStream Stream)
		{
			MemoryCacheEntryOptions Options = new MemoryCacheEntryOptions().SetAbsoluteExpiration(TimeSpan.FromMinutes(1.0));
			StreamCache.Set(Stream.Id, Stream, Options);
		}

		/// <summary>
		/// Gets a cached stream interface
		/// </summary>
		/// <param name="StreamId">Unique id for the stream</param>
		/// <returns>The new stream</returns>
		public async Task<IStream?> GetCachedStream(StreamId StreamId)
		{
			object? Stream;
			if (!StreamCache.TryGetValue(StreamId, out Stream))
			{
				Stream = await GetStreamAsync(StreamId);
				if (Stream != null)
				{
					AddCachedStream((IStream)Stream);
				}
			}
			return (IStream?)Stream;
		}

		/// <summary>
		/// Gets a stream's permissions by ID
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>The stream document</returns>
		public Task<IStreamPermissions?> GetStreamPermissionsAsync(StreamId StreamId)
		{
			return Streams.GetPermissionsAsync(StreamId);
		}

		/// <summary>
		/// Updates the query time on a stream object
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="LastCommitTime">The last query time</param>
		/// <returns>New state of the commit stream</returns>
		public async Task<IStream?> UpdateCommitTimeAsync(IStream? Stream, DateTime LastCommitTime)
		{
			// Try to update the commit stream with the new commits
			while (Stream != null && (!Stream.LastCommitTime.HasValue || LastCommitTime > Stream.LastCommitTime))
			{
				// Try to update the document
				if(await Streams.TryUpdateCommitTimeAsync(Stream, LastCommitTime))
				{
					break;
				}

				// Fetch the new stream document
				Stream = await GetStreamAsync(Stream.Id);
			}
			return Stream;
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="Acl">ACL for the stream to check</param>
		/// <param name="ProjectId">The parent project id</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache of project permissions</param>
		/// <returns>True if the action is authorized</returns>
		private Task<bool> AuthorizeAsync(Acl? Acl, ProjectId ProjectId, AclAction Action, ClaimsPrincipal User, ProjectPermissionsCache? Cache)
		{
			bool? Result = Acl?.Authorize(Action, User);
			if (Result == null)
			{
				return ProjectService.AuthorizeAsync(ProjectId, Action, User, Cache);
			}
			else
			{
				return Task.FromResult(Result.Value);
			}
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="Stream">The stream to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache of project permissions</param>
		/// <returns>True if the action is authorized</returns>
		public Task<bool> AuthorizeAsync(IStream Stream, AclAction Action, ClaimsPrincipal User, ProjectPermissionsCache? Cache)
		{
			return AuthorizeAsync(Stream.Acl, Stream.ProjectId, Action, User, Cache);
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular stream
		/// </summary>
		/// <param name="Stream">The stream to check</param>
		/// <param name="Template">Template within the stream to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache of project permissions</param>
		/// <returns>True if the action is authorized</returns>
		public Task<bool> AuthorizeAsync(IStream Stream, TemplateRef Template, AclAction Action, ClaimsPrincipal User, ProjectPermissionsCache? Cache)
		{
			bool? Result = Template.Acl?.Authorize(Action, User);
			if (Result == null)
			{
				return AuthorizeAsync(Stream, Action, User, Cache);
			}
			else
			{
				return Task.FromResult(Result.Value);
			}
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular project
		/// </summary>
		/// <param name="StreamId">The stream id to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Cache of stream permissions</param>
		/// <returns>True if the action is authorized</returns>
		public async Task<bool> AuthorizeAsync(StreamId StreamId, AclAction Action, ClaimsPrincipal User, StreamPermissionsCache? Cache)
		{
			IStreamPermissions? Permissions;
			if (Cache == null)
			{
				Permissions = await GetStreamPermissionsAsync(StreamId);
			}
			else if (!Cache.Streams.TryGetValue(StreamId, out Permissions))
			{
				Permissions = await GetStreamPermissionsAsync(StreamId);
				Cache.Streams.Add(StreamId, Permissions);
			}
			return Permissions != null && await AuthorizeAsync(Permissions.Acl, Permissions.ProjectId, Action, User, Cache);
		}
	}
}
