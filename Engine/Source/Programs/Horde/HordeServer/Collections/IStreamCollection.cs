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
		/// Creates a new stream
		/// </summary>
		/// <param name="Id">Unique id for the new stream</param>
		/// <param name="Name">Name of the new stream</param>
		/// <param name="ProjectId">Unique id of the project</param>
		/// <param name="ConfigPath">Path to the config file for this stream in Perforce</param>
		/// <param name="Order">Order for this stream</param>
		/// <param name="NotificationChannel">Notification channel for this stream</param>
		/// <param name="NotificationChannelFilter">Notification channel filter for this stream</param>
		/// <param name="TriageChannel">Notification channel for triage requests</param>
		/// <param name="DefaultPreflight">Default template for preflights</param>
		/// <param name="Tabs">Tabs for the new stream</param>
		/// <param name="AgentTypes">Map of new agent types to machine attributes</param>
		/// <param name="WorkspaceTypes">Map of new workspace types</param>
		/// <param name="TemplateRefs">New job templates for the stream</param>
		/// <param name="Properties">New properties for the stream</param>
		/// <param name="Acl">The ACL for this stream</param>
		/// <returns>The new stream document</returns>
		Task<IStream?> TryCreateAsync(StreamId Id, string Name, ProjectId ProjectId, string? ConfigPath = null, int? Order = null, string? NotificationChannel = null, string? NotificationChannelFilter = null, string? TriageChannel = null, DefaultPreflight? DefaultPreflight = null, List<StreamTab>? Tabs = null, Dictionary<string, AgentType>? AgentTypes = null, Dictionary<string, WorkspaceType>? WorkspaceTypes = null, Dictionary<TemplateRefId, TemplateRef>? TemplateRefs = null, Dictionary<string, string>? Properties = null, Acl? Acl = null);

		/// <summary>
		/// Replaces an existing stream
		/// </summary>
		/// <param name="StreamInterface">Unique id for the new stream</param>
		/// <param name="Name">Name of the new stream</param>
		/// <param name="ConfigPath">Path to the config file for this stream in Perforce</param>
		/// <param name="ConfigChange">Changelist number for the config file</param>
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
		/// <param name="Acl">The ACL for this stream</param>
		/// <param name="PausedUntil">The new datetime for pausing builds</param>
		/// <param name="PauseComment">The reason for pausing</param>
		/// <returns>The new stream document</returns>
		Task<IStream?> TryReplaceAsync(IStream StreamInterface, string Name, string? ConfigPath = null, int? ConfigChange = null, int? Order = null, string? NotificationChannel = null, string? NotificationChannelFilter = null, string? TriageChannel = null, DefaultPreflight? DefaultPreflight = null, List<StreamTab>? Tabs = null, Dictionary<string, AgentType>? AgentTypes = null, Dictionary<string, WorkspaceType>? WorkspaceTypes = null, Dictionary<TemplateRefId, TemplateRef>? TemplateRefs = null, Dictionary<string, string>? Properties = null, Acl? Acl = null, DateTime? PausedUntil = null, string? PauseComment = null);

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
