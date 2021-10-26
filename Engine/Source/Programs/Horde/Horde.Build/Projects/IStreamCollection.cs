// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using JobId = ObjectId<IJob>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Collection of stream documents
	/// </summary>
	public interface IStreamCollection
	{
		/// <summary>
		/// Creates or replaces a stream configuration
		/// </summary>
		/// <param name="Id">Unique id for the new stream</param>
		/// <param name="Stream">The current stream value. If not-null, this will attempt to replace the existing instance.</param>
		/// <param name="ConfigPath">Path to the config file</param>
		/// <param name="Revision">The config file revision</param>
		/// <param name="ProjectId">The project id</param>
		/// <param name="Config">The stream configuration</param>
		/// <returns></returns>
		Task<IStream?> TryCreateOrReplaceAsync(StreamId Id, IStream? Stream, string ConfigPath, string Revision, ProjectId ProjectId, StreamConfig Config);

		/// <summary>
		/// Gets a stream by ID
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>The stream document</returns>
		Task<IStream?> GetAsync(StreamId StreamId);

		/// <summary>
		/// Gets a stream's permissions by ID
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>The stream document</returns>
		Task<IStreamPermissions?> GetPermissionsAsync(StreamId StreamId);

		/// <summary>
		/// Enumerates all streams
		/// </summary>
		/// <returns></returns>
		Task<List<IStream>> FindAllAsync();

		/// <summary>
		/// Gets all the available streams for a project
		/// </summary>
		/// <param name="ProjectIds">Unique id of the projects to query</param>
		/// <returns>List of stream documents</returns>
		Task<List<IStream>> FindForProjectsAsync(ProjectId[] ProjectIds);

		/// <summary>
		/// Updates user-facing properties for an existing stream
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="NewPausedUntil">The new datetime for pausing builds</param>
		/// <param name="NewPauseComment">The reason for pausing</param>
		/// <returns>The updated stream if successful, null otherwise</returns>
		Task<IStream?> TryUpdatePauseStateAsync(IStream Stream, DateTime? NewPausedUntil, string? NewPauseComment);

		/// <summary>
		/// Attempts to update the last trigger time for a schedule
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="TemplateRefId">The template ref id</param>
		/// <param name="LastTriggerTime">New last trigger time for the schedule</param>
		/// <param name="LastTriggerChange">New last trigger changelist for the schedule</param>
		/// <param name="NewActiveJobs">New list of active jobs</param>
		/// <returns>The updated stream if successful, null otherwise</returns>
		Task<IStream?> TryUpdateScheduleTriggerAsync(IStream Stream, TemplateRefId TemplateRefId, DateTimeOffset? LastTriggerTime, int? LastTriggerChange, List<JobId> NewActiveJobs);

		/// <summary>
		/// Delete a stream
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(StreamId StreamId);
	}

	static class StreamCollectionExtensions
	{
		/// <summary>
		/// Creates or replaces a stream configuration
		/// </summary>
		/// <param name="StreamCollection">The stream collection</param>
		/// <param name="Id">Unique id for the new stream</param>
		/// <param name="Stream">The current stream value. If not-null, this will attempt to replace the existing instance.</param>
		/// <param name="ConfigPath">Path to the config file</param>
		/// <param name="Revision">The config file revision</param>
		/// <param name="ProjectId">The project id</param>
		/// <param name="Config">The stream configuration</param>
		/// <returns></returns>
		public static async Task<IStream> CreateOrReplaceAsync(this IStreamCollection StreamCollection, StreamId Id, IStream? Stream, string ConfigPath, string Revision, ProjectId ProjectId, StreamConfig Config)
		{
			for (; ; )
			{
				Stream = await StreamCollection.TryCreateOrReplaceAsync(Id, Stream, ConfigPath, Revision, ProjectId, Config);
				if (Stream != null)
				{
					return Stream;
				}
				Stream = await StreamCollection.GetAsync(Id);
			}
		}
	}
}
