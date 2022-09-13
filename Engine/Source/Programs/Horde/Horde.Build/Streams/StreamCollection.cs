// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Acls;
using Horde.Build.Configuration;
using Horde.Build.Jobs;
using Horde.Build.Jobs.Schedules;
using Horde.Build.Jobs.Templates;
using Horde.Build.Perforce;
using Horde.Build.Projects;
using Horde.Build.Server;
using Horde.Build.Users;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.IO;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;

namespace Horde.Build.Streams
{
	using JobId = ObjectId<IJob>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateId = StringId<ITemplateRef>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Collection of stream documents
	/// </summary>
	public class StreamCollection : IStreamCollection
	{
		/// <summary>
		/// Information about a stream
		/// </summary>
		class StreamDoc : IStream
		{
			[BsonRequired, BsonId]
			public StreamId Id { get; set; }

			[BsonRequired]
			public ProjectId ProjectId { get; set; }

			public string ConfigRevision { get; set; } = String.Empty;

			public Dictionary<TemplateId, TemplateRefDoc> Templates { get; set; } = new Dictionary<TemplateId, TemplateRefDoc>();
			public DateTime? PausedUntil { get; set; }
			public string? PauseComment { get; set; }

			public Acl? Acl { get; set; }
			public int UpdateIndex { get; set; }
			public bool Deleted { get; set; }

			[BsonConstructor]
			private StreamDoc()
			{
			}

			public StreamDoc(StreamId id, ProjectId projectId)
			{
				Id = id;
				ProjectId = projectId;
			}

			#region IStream Implementation

			[BsonIgnore]
			public string Name => Config?.Name!;

			[BsonIgnore]
			public StreamConfig Config { get; private set; } = null!;

			[BsonIgnore]
			IReadOnlyDictionary<TemplateId, ITemplateRef>? _cachedTemplates;

			IReadOnlyDictionary<TemplateId, ITemplateRef> IStream.Templates
			{
				get
				{
					_cachedTemplates ??= Templates.ToDictionary(x => x.Key, x => (ITemplateRef)x.Value);
					return _cachedTemplates;
				}
			}

			public async Task PostLoadAsync(ConfigCollection configCollection, ILogger logger)
			{
				try
				{
					Config = await configCollection.GetConfigAsync<StreamConfig>(ConfigRevision);
				}
				catch (Exception)
				{
					if (Deleted)
					{
						logger.LogWarning("Unable to get stream config for {StreamId} at {Revision}; using default.", Id, ConfigRevision);
						Config = new StreamConfig();
					}
					else
					{
						logger.LogError("Unable to get stream config for {StreamId} at {Revision}", Id, ConfigRevision);
						throw;
					}
				}

				foreach (KeyValuePair<TemplateId, TemplateRefDoc> pair in Templates)
				{
					pair.Value.PostLoad(this, pair.Key);
				}
			}

			#endregion
		}

		class TemplateRefDoc : ITemplateRef
		{
			[BsonRequired]
			public ContentHash Hash { get; set; } = null!;

			[BsonIgnoreIfNull]
			public TemplateScheduleDoc? Schedule { get; set; }

			[BsonIgnoreIfNull]
			public List<TemplateStepDoc>? StepStates { get; set; }

			[BsonIgnoreIfNull]
			public Acl? Acl { get; set; }

			#region ITemplateRef implementation

			[BsonIgnore]
			StreamDoc? _owner;

			[BsonIgnore]
			public TemplateId Id { get; private set; }

			[BsonIgnore]
			TemplateRefConfig? _config;

			[BsonIgnore]
			public TemplateRefConfig Config
			{
				get
				{
					if (_config == null && _owner != null && _owner.Config != null)
					{
						// This should generally always succeed, but adding a fallback to handle legacy data.
						_config = _owner.Config.Templates.FirstOrDefault(x => x.Id == Id) ?? new TemplateRefConfig { Id = Id, Name = Id.ToString() };
					}
					return _config!;
				}
			}

			ITemplateSchedule? ITemplateRef.Schedule => Schedule;
			IReadOnlyList<ITemplateStep> ITemplateRef.StepStates => (IReadOnlyList<ITemplateStep>?)StepStates ?? Array.Empty<ITemplateStep>();

			public void PostLoad(StreamDoc owner, TemplateId id)
			{
				_owner = owner;
				Id = id;
				Schedule?.PostLoad(this);

				if (StepStates != null)
				{
					StepStates.RemoveAll(x => x.PausedByUserId == null);
				}
			}

			#endregion
		}

		class TemplateScheduleDoc : ITemplateSchedule
		{
			public int LastTriggerChange { get; set; }

			[BsonIgnoreIfNull, Obsolete("Use LastTriggerTimeUtc instead")]
			public DateTimeOffset? LastTriggerTime { get; set; }

			public DateTime LastTriggerTimeUtc { get; set; }
			public List<JobId> ActiveJobs { get; set; } = new List<JobId>();

			#region ITemplateSchedule implementation

			[BsonIgnore]
			TemplateRefDoc? _owner;

			[BsonIgnore]
			public ScheduleConfig Config => _owner?.Config.Schedule!;

			IReadOnlyList<JobId> ITemplateSchedule.ActiveJobs => ActiveJobs;

			public void PostLoad(TemplateRefDoc owner)
			{
				_owner = owner;

#pragma warning disable CS0618 // Type or member is obsolete
				if (LastTriggerTime.HasValue)
				{
					LastTriggerTimeUtc = LastTriggerTime.Value.UtcDateTime;
					LastTriggerTime = null;
				}
#pragma warning restore CS0618 // Type or member is obsolete
			}

			#endregion
		}

		class TemplateStepDoc : ITemplateStep
		{
			public string Name { get; set; } = String.Empty;
			public UserId? PausedByUserId { get; set; }
			public DateTime? PauseTimeUtc { get; set; }

			UserId ITemplateStep.PausedByUserId => PausedByUserId ?? UserId.Empty;
			DateTime ITemplateStep.PauseTimeUtc => PauseTimeUtc ?? DateTime.MinValue;

			public TemplateStepDoc()
			{
			}

			public TemplateStepDoc(string name, UserId pausedByUserId, DateTime pauseTimeUtc)
			{
				Name = name;
				PausedByUserId = pausedByUserId;
				PauseTimeUtc = pauseTimeUtc;
			}
		}

		/// <summary>
		/// Projection of a stream definition to just include permissions info
		/// </summary>
		[SuppressMessage("Design", "CA1812: Class is never instantiated")]
		private class StreamPermissions : IStreamPermissions
		{
			public Acl? Acl { get; set; }
			public ProjectId ProjectId { get; set; }

			public static readonly ProjectionDefinition<StreamDoc> Projection = Builders<StreamDoc>.Projection.Include(x => x.Acl).Include(x => x.ProjectId);
		}

		readonly IMongoCollection<StreamDoc> _streams;
		readonly ConfigCollection _configCollection;
		readonly IClock _clock;
		readonly ITemplateCollection _templateCollection;
		readonly ILogger<StreamCollection> _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService">The database service instance</param>
		/// <param name="configCollection"></param>
		/// <param name="clock"></param>
		/// <param name="templateCollection"></param>
		/// <param name="logger"></param>
		public StreamCollection(MongoService mongoService, ConfigCollection configCollection, IClock clock, ITemplateCollection templateCollection, ILogger<StreamCollection> logger)
		{
			_streams = mongoService.GetCollection<StreamDoc>("Streams");
			_configCollection = configCollection;
			_clock = clock;
			_templateCollection = templateCollection;
			_logger = logger;
		}

		Task PostLoadAsync(StreamDoc stream) => stream.PostLoadAsync(_configCollection, _logger);

		/// <inheritdoc/>
		public async Task<IStream?> TryCreateOrReplaceAsync(StreamId id, IStream? stream, string revision, ProjectId projectId)
		{
			StreamConfig config = await _configCollection.GetConfigAsync<StreamConfig>(revision);
			Validate(id, config);

			StreamDoc? streamDoc = (StreamDoc?)stream;
			Dictionary<TemplateId, TemplateRefDoc> templateRefs = await CreateTemplateRefsAsync(streamDoc, config.Templates);

			Acl? acl = Acl.Merge(new Acl(), config.Acl);
			if (streamDoc == null)
			{
				return await TryCreateAsync(id, projectId, revision, templateRefs, acl);
			}
			else
			{
				return await TryReplaceAsync(streamDoc, projectId, revision, config, templateRefs, acl);
			}
		}

		/// <summary>
		/// Creates a list of template refs from a set of request objects
		/// </summary>
		/// <param name="requests">Request objects</param>
		/// <param name="stream">The current stream state</param>
		/// <returns>List of new template references</returns>
		async Task<Dictionary<TemplateId, TemplateRefDoc>> CreateTemplateRefsAsync(StreamDoc? stream, List<TemplateRefConfig> requests)
		{
			Dictionary<TemplateId, TemplateRefDoc> newTemplateRefs = new Dictionary<TemplateId, TemplateRefDoc>();
			foreach (TemplateRefConfig request in requests)
			{
				// Create the template
				ITemplate template = await _templateCollection.AddAsync(request.Name, request.Priority, request.AllowPreflights, request.UpdateIssues, request.PromoteIssuesByDefault, request.InitialAgentType, request.SubmitNewChange, request.SubmitDescription, request.Arguments, request.Parameters.ConvertAll(x => x.ToModel()));

				// Get an identifier for the new template ref
				TemplateId templateId = request.Id;

				// Get the existing template ref or create a new one
				TemplateRefDoc? templateRef;
				if (stream == null || !stream.Templates.TryGetValue(templateId, out templateRef))
				{
					templateRef = new TemplateRefDoc();
				}

				// Update the hash
				templateRef.Hash = template.Id;
				if (request.Schedule != null)
				{
					templateRef.Schedule ??= new TemplateScheduleDoc();
					templateRef.Schedule.LastTriggerTimeUtc = _clock.UtcNow;
				}

				// Add it to the new lookup
				newTemplateRefs.Add(templateId, templateRef);
			}
			return newTemplateRefs;
		}

		/// <inheritdoc/>
		public async Task<IStream?> TryUpdateTemplateRefAsync(IStream streamInterface, TemplateId templateId, List<UpdateStepStateRequest>? stepStates = null)
		{
			StreamDoc stream = (StreamDoc)streamInterface;

			TemplateRefDoc? templateRef;
			if (!stream.Templates.TryGetValue(templateId, out templateRef))
			{
				return null;
			}

			UpdateDefinitionBuilder<StreamDoc> updateBuilder = Builders<StreamDoc>.Update;
			List<UpdateDefinition<StreamDoc>> updates = new List<UpdateDefinition<StreamDoc>>();

			Dictionary<TemplateId, TemplateRefDoc> newTemplates = new Dictionary<TemplateId, TemplateRefDoc>(stream.Templates);

			// clear
			if (stepStates != null && stepStates.Count == 0)
			{
				bool hasUpdates = false;
				foreach (KeyValuePair<TemplateId, TemplateRefDoc> entry in newTemplates)
				{
					if (entry.Value.StepStates != null)
					{
						hasUpdates = true;
						entry.Value.StepStates = null;
					}
				}

				if (hasUpdates)
				{
					updates.Add(updateBuilder.Set(x => x.Templates, newTemplates));
				}
			}
			else if (stepStates != null)
			{
				// get currently valid step states
				List<TemplateStepDoc> newStepStates = templateRef.StepStates?.ToList() ?? new List<TemplateStepDoc>();

				// generate update list
				foreach (UpdateStepStateRequest updateState in stepStates)
				{
					int stateIndex = newStepStates.FindIndex(x => x.Name == updateState.Name);

					UserId? pausedByUserId = updateState.PausedByUserId != null ? new UserId(updateState.PausedByUserId) : null;

					if (stateIndex == -1)
					{
						// if this is a new state without anything set, ignore it
						if (pausedByUserId != null)
						{
							newStepStates.Add(new TemplateStepDoc(updateState.Name, pausedByUserId.Value, _clock.UtcNow));
						}
					}
					else
					{
						if (pausedByUserId == null)
						{
							newStepStates.RemoveAt(stateIndex);
						}
						else
						{
							newStepStates[stateIndex].PausedByUserId = pausedByUserId.Value;
						}
					}
				}

				if (newStepStates.Count == 0)
				{
					templateRef.StepStates = null;
				}
				else
				{
					templateRef.StepStates = newStepStates;
				}

				updates.Add(updateBuilder.Set(x => x.Templates, newTemplates));
			}

			if (updates.Count == 0)
			{
				return streamInterface;
			}

			return await TryUpdateStreamAsync(stream, updateBuilder.Combine(updates));

		}

		/// <inheritdoc/>
		async Task<IStream?> TryCreateAsync(StreamId id, ProjectId projectId, string configRevision, Dictionary<TemplateId, TemplateRefDoc> templateRefs, Acl? acl)
		{
			StreamDoc newStream = new StreamDoc(id, projectId);
			newStream.ConfigRevision = configRevision;
			newStream.Templates = templateRefs;
			newStream.Acl = acl;

			try
			{
				await _streams.InsertOneAsync(newStream);
				await PostLoadAsync(newStream);
				return newStream;
			}
			catch (MongoWriteException ex)
			{
				if (ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
				{
					return null;
				}
				else
				{
					throw;
				}
			}
		}

		/// <inheritdoc/>
		async Task<IStream?> TryReplaceAsync(StreamDoc stream, ProjectId projectId, string configRevision, StreamConfig config, Dictionary<TemplateId, TemplateRefDoc> templateRefs, Acl? acl)
		{
			UpdateDefinitionBuilder<StreamDoc> updateBuilder = Builders<StreamDoc>.Update;

			List<UpdateDefinition<StreamDoc>> updates = new List<UpdateDefinition<StreamDoc>>();
			updates.Add(updateBuilder.Set(x => x.ProjectId, projectId));
			updates.Add(updateBuilder.Set(x => x.ConfigRevision, configRevision));
			updates.Add(updateBuilder.Set(x => x.Templates, templateRefs));
			updates.Add(updateBuilder.SetOrUnsetNullRef(x => x.Acl, acl));
			updates.Add(updateBuilder.Unset(x => x.Deleted));

			return await TryUpdateStreamAsync(stream, updateBuilder.Combine(updates));
		}

		/// <inheritdoc/>
		public async Task<IStream?> GetAsync(StreamId streamId)
		{
			StreamDoc? stream = await _streams.Find<StreamDoc>(x => x.Id == streamId).FirstOrDefaultAsync();
			if (stream != null)
			{
				await PostLoadAsync(stream);
			}
			return stream;
		}

		/// <inheritdoc/>
		public async Task<IStreamPermissions?> GetPermissionsAsync(StreamId streamId)
		{
			return await _streams.Find<StreamDoc>(x => x.Id == streamId).Project<StreamPermissions>(StreamPermissions.Projection).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<IStream>> FindAllAsync()
		{
			List<StreamDoc> results = await _streams.Find(Builders<StreamDoc>.Filter.Ne(x => x.Deleted, true)).ToListAsync();
			foreach (StreamDoc result in results)
			{
				await PostLoadAsync(result);
			}
			return results.ConvertAll<IStream>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<IStream>> FindForProjectsAsync(ProjectId[] projectIds)
		{
			FilterDefinition<StreamDoc> filter = Builders<StreamDoc>.Filter.In(x => x.ProjectId, projectIds) & Builders<StreamDoc>.Filter.Ne(x => x.Deleted, true);

			List<StreamDoc> results = await _streams.Find(filter).ToListAsync();
			foreach (StreamDoc result in results)
			{
				await PostLoadAsync(result);
			}
			return results.ConvertAll<IStream>(x => x);
		}

		/// <inheritdoc/>
		public async Task<IStream?> TryUpdatePauseStateAsync(IStream streamInterface, DateTime? newPausedUntil, string? newPauseComment)
		{
			StreamDoc stream = (StreamDoc)streamInterface;

			UpdateDefinitionBuilder<StreamDoc> updateBuilder = Builders<StreamDoc>.Update;

			List<UpdateDefinition<StreamDoc>> updates = new List<UpdateDefinition<StreamDoc>>();
			stream.PausedUntil = newPausedUntil;
			stream.PauseComment = newPauseComment;
			updates.Add(updateBuilder.Set(x => x.PausedUntil, newPausedUntil));
			updates.Add(updateBuilder.Set(x => x.PauseComment, newPauseComment));

			return await TryUpdateStreamAsync(stream, updateBuilder.Combine(updates));
		}

		/// <inheritdoc/>
		public async Task<IStream?> TryUpdateScheduleTriggerAsync(IStream streamInterface, TemplateId templateId, DateTime? lastTriggerTimeUtc, int? lastTriggerChange, List<JobId> newActiveJobs)
		{
			StreamDoc stream = (StreamDoc)streamInterface;
			TemplateRefDoc template = stream.Templates[templateId];
			TemplateScheduleDoc schedule = template.Schedule!;

			// Build the updates. MongoDB driver cannot parse TemplateRefId in expression tree; need to specify field name explicitly
			List<UpdateDefinition<StreamDoc>> updates = new List<UpdateDefinition<StreamDoc>>();
			if (lastTriggerTimeUtc.HasValue && lastTriggerTimeUtc.Value != schedule.LastTriggerTimeUtc)
			{
				FieldDefinition<StreamDoc, DateTime> lastTriggerTimeField = $"{nameof(stream.Templates)}.{templateId}.{nameof(template.Schedule)}.{nameof(schedule.LastTriggerTimeUtc)}";
				updates.Add(Builders<StreamDoc>.Update.Set(lastTriggerTimeField, lastTriggerTimeUtc.Value));
				schedule.LastTriggerTimeUtc = lastTriggerTimeUtc.Value;
			}
			if (lastTriggerChange.HasValue && lastTriggerChange.Value > schedule.LastTriggerChange)
			{
				FieldDefinition<StreamDoc, int> lastTriggerChangeField = $"{nameof(stream.Templates)}.{templateId}.{nameof(template.Schedule)}.{nameof(schedule.LastTriggerChange)}";
				updates.Add(Builders<StreamDoc>.Update.Set(lastTriggerChangeField, lastTriggerChange.Value));
				schedule.LastTriggerChange = lastTriggerChange.Value;
			}
			if (newActiveJobs != null)
			{
				FieldDefinition<StreamDoc, List<JobId>> field = $"{nameof(stream.Templates)}.{templateId}.{nameof(template.Schedule)}.{nameof(schedule.ActiveJobs)}";
				updates.Add(Builders<StreamDoc>.Update.Set(field, newActiveJobs));
				schedule.ActiveJobs = newActiveJobs;
			}

			return (updates.Count == 0)? streamInterface : await TryUpdateStreamAsync(stream, Builders<StreamDoc>.Update.Combine(updates));
		}

		/// <summary>
		/// Update a stream
		/// </summary>
		/// <param name="stream">The stream to update</param>
		/// <param name="update">The update definition</param>
		/// <returns>The updated document, or null the update failed</returns>
		private async Task<StreamDoc?> TryUpdateStreamAsync(StreamDoc stream, UpdateDefinition<StreamDoc> update)
		{
			FilterDefinition<StreamDoc> filter = Builders<StreamDoc>.Filter.Expr(x => x.Id == stream.Id && x.UpdateIndex == stream.UpdateIndex);
			update = update.Set(x => x.UpdateIndex, stream.UpdateIndex + 1);

			FindOneAndUpdateOptions<StreamDoc> options = new FindOneAndUpdateOptions<StreamDoc> { ReturnDocument = ReturnDocument.After };

			StreamDoc? result = await _streams.FindOneAndUpdateAsync(filter, update, options);
			if(result != null)
			{
				await PostLoadAsync(result);
			}
			return result;
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(StreamId streamId)
		{
			await _streams.UpdateOneAsync<StreamDoc>(x => x.Id == streamId, Builders<StreamDoc>.Update.Set(x => x.Deleted, true).Inc(x => x.UpdateIndex, 1));
		}

		/// <summary>
		/// Checks the stream definition for consistency
		/// </summary>
		public static void Validate(StreamId streamId, StreamConfig config)
		{
			HashSet<TemplateId> remainingTemplates = new HashSet<TemplateId>(config.Templates.Select(x => x.Id));

			// Check the default preflight template is valid
			if (config.DefaultPreflight != null)
			{
				if (config.DefaultPreflight.TemplateId != null && !remainingTemplates.Contains(config.DefaultPreflight.TemplateId.Value))
				{
					throw new InvalidStreamException($"Default preflight template was listed as '{config.DefaultPreflight.TemplateId.Value}', but no template was found by that name");
				}
			}

			// Check the chained jobs are valid
			foreach (TemplateRefConfig templateRef in config.Templates)
			{
				if (templateRef.ChainedJobs != null)
				{
					foreach (ChainedJobTemplateConfig chainedJob in templateRef.ChainedJobs)
					{
						if (!remainingTemplates.Contains(chainedJob.TemplateId))
						{
							throw new InvalidDataException($"Invalid template ref id '{chainedJob.TemplateId}");
						}
					}
				}
			}

			HashSet<TemplateId> undefinedTemplates = new();
			// Check that all the templates are referenced by a tab
			foreach (JobsTabConfig jobsTab in config.Tabs.OfType<JobsTabConfig>())
			{
				if (jobsTab.Templates != null)
				{
					remainingTemplates.ExceptWith(jobsTab.Templates);
					foreach (TemplateId templateId in jobsTab.Templates)
					{
						if (config.Templates.Find(x => x.Id == templateId) == null)
						{
							undefinedTemplates.Add(templateId);
						}
					}
					
				}
			}
			if (remainingTemplates.Count > 0)
			{
				throw new InvalidStreamException(String.Join("\n", remainingTemplates.Select(x => $"Template '{x}' is not listed on any tab for {streamId}")));
			}

			if (undefinedTemplates.Count > 0)
			{
				throw new InvalidStreamException(String.Join("\n", undefinedTemplates.Select(x => $"Template '{x}' is not defined for {streamId}")));
			}

			// Check that all the agent types reference valid workspace names
			foreach (KeyValuePair<string, AgentConfig> pair in config.AgentTypes)
			{
				string? workspaceTypeName = pair.Value.Workspace;
				if (workspaceTypeName != null && !config.WorkspaceTypes.ContainsKey(workspaceTypeName))
				{
					throw new InvalidStreamException($"Agent type '{pair.Key}' references undefined workspace type '{pair.Value.Workspace}' in {streamId}");
				}
			}
		}
	}
}
