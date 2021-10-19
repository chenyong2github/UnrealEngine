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
using HordeServer.Collections;
using Microsoft.Extensions.Caching.Memory;

namespace HordeServer.Services
{
	using JobId = ObjectId<IJob>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

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
		/// Accessor for the stream collection
		/// </summary>
		public IStreamCollection StreamCollection => Streams;

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
		/// Deletes an existing stream
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>Async task object</returns>
		public async Task DeleteStreamAsync(StreamId StreamId)
		{
			await Streams.DeleteAsync(StreamId);
		}

		/// <summary>
		/// Updates an existing stream
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="NewPausedUntil">The new datetime for pausing builds</param>
		/// <param name="NewPauseComment">The reason for pausing</param>
		/// <returns>Async task object</returns>
		public async Task<IStream?> UpdatePauseStateAsync(IStream? Stream, DateTime? NewPausedUntil = null, string? NewPauseComment = null)
		{
			for (; Stream != null; Stream = await GetStreamAsync(Stream.Id))
			{
				IStream? NewStream = await Streams.TryUpdatePauseStateAsync(Stream, NewPausedUntil, NewPauseComment);
				if (NewStream != null)
				{
					return NewStream;
				}
			}
			return null;
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
		public async Task<IStream?> UpdateScheduleTriggerAsync(IStream Stream, TemplateRefId TemplateRefId, DateTimeOffset? LastTriggerTime, int? LastTriggerChange, List<JobId> AddJobs, List<JobId> RemoveJobs)
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

				List<JobId> NewActiveJobs = TemplateRef.Schedule.ActiveJobs.Except(RemoveJobs).Union(AddJobs).ToList();

				NewStream = await Streams.TryUpdateScheduleTriggerAsync(NewStream, TemplateRefId, LastTriggerTime, LastTriggerChange, NewActiveJobs);

				if (NewStream != null)
				{
					return NewStream;
				}

				NewStream = await Streams.GetAsync(Stream.Id);
			}
			return null;
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
