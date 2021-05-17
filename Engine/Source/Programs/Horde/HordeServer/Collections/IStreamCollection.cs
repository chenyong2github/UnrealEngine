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

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServer.Collections
{
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
		/// <param name="Revision">The config file revision</param>
		/// <param name="ProjectId">The project id</param>
		/// <param name="Config">The stream configuration</param>
		/// <returns></returns>
		Task<IStream?> TryCreateOrReplaceAsync(StreamId Id, IStream? Stream, string Revision, ProjectId ProjectId, StreamConfig Config);

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
		/// <param name="NewName">The new name for the stream</param>
		/// <param name="NewOrder">New order for the stream</param>
		/// <param name="NewNotificationChannel">New notification channel for the stream</param>
		/// <param name="NewNotificationChannelFilter">New notification channel filter for the stream</param>
		/// <param name="NewTriageChannel">New triage channel</param>
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
		Task<bool> TryUpdatePropertiesAsync(IStream Stream, string? NewName, int? NewOrder, string? NewNotificationChannel, string? NewNotificationChannelFilter, string? NewTriageChannel, List<StreamTab>? NewTabs, Dictionary<string, AgentType?>? NewAgentTypes, Dictionary<string, WorkspaceType?>? NewWorkspaceTypes, Dictionary<TemplateRefId, TemplateRef>? NewTemplateRefs, Dictionary<string, string?>? NewProperties, Acl? NewAcl, bool? UpdatePauseFields, DateTime? NewPausedUntil, string? NewPauseComment);

		/// <summary>
		/// Attempts to update the last commit time for a streams
		/// </summary>
		/// <param name="Stream"></param>
		/// <param name="LastCommitTime"></param>
		/// <returns></returns>
		Task<bool> TryUpdateCommitTimeAsync(IStream Stream, DateTime LastCommitTime);

		/// <summary>
		/// Attempts to update the last trigger time for a schedule
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="TemplateRefId">The template ref id</param>
		/// <param name="LastTriggerTime">New last trigger time for the schedule</param>
		/// <param name="LastTriggerChange">New last trigger changelist for the schedule</param>
		/// <param name="NewActiveJobs">New list of active jobs</param>
		/// <returns>True if the stream was updated</returns>
		Task<bool> TryUpdateScheduleTriggerAsync(IStream Stream, TemplateRefId TemplateRefId, DateTimeOffset? LastTriggerTime, int? LastTriggerChange, List<ObjectId> NewActiveJobs);

		/// <summary>
		/// Delete a stream
		/// </summary>
		/// <param name="StreamId">Unique id of the stream</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(StreamId StreamId);
	}
}
