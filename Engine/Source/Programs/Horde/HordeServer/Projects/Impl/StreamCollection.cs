// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
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
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using JobId = ObjectId<IJob>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Collection of stream documents
	/// </summary>
	public class StreamCollection : IStreamCollection
	{
		/// <summary>
		/// Information about a stream
		/// </summary>
		class StreamDocument : IStream
		{
			public const int DefaultOrder = 128;

			[BsonRequired, BsonId]
			public StreamId Id { get; set; }

			[BsonRequired]
			public ProjectId ProjectId { get; set; }

			[BsonRequired]
			public string Name { get; set; }

			public string? ClusterName { get; set; }
			public string ConfigPath { get; set; } = String.Empty;
			public string ConfigRevision { get; set; } = String.Empty;

			public int Order { get; set; } = DefaultOrder;
			public string? NotificationChannel { get; set; }
			public string? NotificationChannelFilter { get; set; }
			public string? TriageChannel { get; set; }
			public DefaultPreflight? DefaultPreflight { get; set; }
			public List<StreamTab> Tabs { get; set; } = new List<StreamTab>();
			public Dictionary<string, AgentType> AgentTypes { get; set; } = new Dictionary<string, AgentType>(StringComparer.Ordinal);
			public Dictionary<string, WorkspaceType> WorkspaceTypes { get; set; } = new Dictionary<string, WorkspaceType>(StringComparer.Ordinal);
			public Dictionary<TemplateRefId, TemplateRef> Templates { get; set; } = new Dictionary<TemplateRefId, TemplateRef>();
			public DateTime? PausedUntil { get; set; }
			public string? PauseComment { get; set; }
			public Acl? Acl { get; set; }
			public int UpdateIndex { get; set; }
			public bool Deleted { get; set; }

			string IStream.ClusterName => ClusterName ?? PerforceCluster.DefaultName;
			IReadOnlyList<StreamTab> IStream.Tabs => Tabs;
			IReadOnlyDictionary<string, AgentType> IStream.AgentTypes => AgentTypes;
			IReadOnlyDictionary<string, WorkspaceType> IStream.WorkspaceTypes => WorkspaceTypes;
			IReadOnlyDictionary<TemplateRefId, TemplateRef> IStream.Templates => Templates;

			[BsonConstructor]
			private StreamDocument()
			{
				Name = null!;
			}

			public StreamDocument(StreamId Id, string Name, ProjectId ProjectId)
			{
				this.Id = Id;
				this.Name = Name;
				this.ProjectId = ProjectId;
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

			public static readonly ProjectionDefinition<StreamDocument> Projection = Builders<StreamDocument>.Projection.Include(x => x.Acl).Include(x => x.ProjectId);
		}

		/// <summary>
		/// The stream collection
		/// </summary>
		IMongoCollection<StreamDocument> Streams;

		/// <summary>
		/// Clock
		/// </summary>
		IClock Clock;

		/// <summary>
		/// The template collection
		/// </summary>
		ITemplateCollection TemplateCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="Clock"></param>
		/// <param name="TemplateCollection"></param>
		public StreamCollection(DatabaseService DatabaseService, IClock Clock, ITemplateCollection TemplateCollection)
		{
			this.Streams = DatabaseService.GetCollection<StreamDocument>("Streams");
			this.Clock = Clock;
			this.TemplateCollection = TemplateCollection;
		}

		/// <inheritdoc/>
		public async Task<IStream?> TryCreateOrReplaceAsync(StreamId Id, IStream? Stream, string ConfigPath, string Revision, ProjectId ProjectId, StreamConfig Config)
		{
			List<StreamTab> Tabs = Config.Tabs.ConvertAll(x => StreamTab.FromRequest(x));
			Dictionary<TemplateRefId, TemplateRef> TemplateRefs = await CreateTemplateRefsAsync(Config.Templates, Stream, TemplateCollection);

			Dictionary<string, AgentType> AgentTypes = new Dictionary<string, AgentType>();
			if (Config.AgentTypes != null)
			{
				AgentTypes = Config.AgentTypes.Where(x => x.Value != null).ToDictionary(x => x.Key, x => new AgentType(x.Value!));
			}

			Dictionary<string, WorkspaceType> WorkspaceTypes = new Dictionary<string, WorkspaceType>();
			if (Config.WorkspaceTypes != null)
			{
				WorkspaceTypes = Config.WorkspaceTypes.Where(x => x.Value != null).ToDictionary(x => x.Key, x => new WorkspaceType(x.Value!));
			}

			DefaultPreflight? DefaultPreflight = Config.DefaultPreflight?.ToModel();
			if (DefaultPreflight == null && Config.DefaultPreflightTemplate != null)
			{
				DefaultPreflight = new DefaultPreflight(new TemplateRefId(Config.DefaultPreflightTemplate), null);
			}

			Validate(Id, DefaultPreflight, TemplateRefs, Tabs, AgentTypes, WorkspaceTypes);

			Acl? Acl = Acl.Merge(new Acl(), Config.Acl);
			if (Stream == null)
			{
				return await TryCreateAsync(Id, ProjectId, ConfigPath, Revision, Config, DefaultPreflight, Tabs, AgentTypes, WorkspaceTypes, TemplateRefs, Acl);
			}
			else
			{
				return await TryReplaceAsync(Stream, ProjectId, ConfigPath, Revision, Config, DefaultPreflight, Tabs, AgentTypes, WorkspaceTypes, TemplateRefs, Acl);
			}
		}

		/// <summary>
		/// Creates a list of template refs from a set of request objects
		/// </summary>
		/// <param name="Requests">Request objects</param>
		/// <param name="Stream">The current stream state</param>
		/// <param name="TemplateCollection">The template service</param>
		/// <returns>List of new template references</returns>
		async Task<Dictionary<TemplateRefId, TemplateRef>> CreateTemplateRefsAsync(List<CreateTemplateRefRequest> Requests, IStream? Stream, ITemplateCollection TemplateCollection)
		{
			Dictionary<TemplateRefId, TemplateRef> NewTemplateRefs = new Dictionary<TemplateRefId, TemplateRef>();
			foreach (CreateTemplateRefRequest Request in Requests)
			{
				// Create the template
				ITemplate NewTemplate = await TemplateCollection.AddAsync(Request.Name, Request.Priority, Request.AllowPreflights, Request.InitialAgentType, Request.SubmitNewChange, Request.Arguments, Request.Parameters.ConvertAll(x => x.ToModel()));

				// Get an identifier for the new template ref
				TemplateRefId NewTemplateRefId;
				if (Request.Id != null)
				{
					NewTemplateRefId = new TemplateRefId(Request.Id);
				}
				else
				{
					NewTemplateRefId = TemplateRefId.Sanitize(Request.Name);
				}

				// Create the schedule object
				Schedule? Schedule = null;
				if (Request.Schedule != null)
				{
					Schedule = Request.Schedule.ToModel();
					Schedule.LastTriggerTime = Clock.UtcNow;
				}

				// Add it to the list
				TemplateRef NewTemplateRef = new TemplateRef(NewTemplate, Request.ShowUgsBadges, Request.ShowUgsAlerts, Request.NotificationChannel, Request.NotificationChannelFilter, Request.TriageChannel, Schedule, Request.ChainedJobs?.ConvertAll(x => new ChainedJobTemplate(x)), Acl.Merge(null, Request.Acl));
				if (Stream != null && Stream.Templates.TryGetValue(NewTemplateRefId, out TemplateRef? OldTemplateRef))
				{
					if (OldTemplateRef.Schedule != null && NewTemplateRef.Schedule != null)
					{
						NewTemplateRef.Schedule.CopyState(OldTemplateRef.Schedule);
					}
				}
				NewTemplateRefs.Add(NewTemplateRefId, NewTemplateRef);
			}
			foreach (TemplateRef TemplateRef in NewTemplateRefs.Values)
			{
				if (TemplateRef.ChainedJobs != null)
				{
					foreach (ChainedJobTemplate ChainedJob in TemplateRef.ChainedJobs)
					{
						if (!NewTemplateRefs.ContainsKey(ChainedJob.TemplateRefId))
						{
							throw new InvalidDataException($"Invalid template ref id '{ChainedJob.TemplateRefId}");
						}
					}
				}
			}
			return NewTemplateRefs;
		}

		/// <inheritdoc/>
		async Task<IStream?> TryCreateAsync(StreamId Id, ProjectId ProjectId, string ConfigPath, string ConfigRevision, StreamConfig Config, DefaultPreflight? DefaultPreflight, List<StreamTab> Tabs, Dictionary<string, AgentType> AgentTypes, Dictionary<string, WorkspaceType> WorkspaceTypes, Dictionary<TemplateRefId, TemplateRef> TemplateRefs, Acl? Acl)
		{
			StreamDocument NewStream = new StreamDocument(Id, Config.Name, ProjectId);
			NewStream.ClusterName = Config.ClusterName;
			NewStream.ConfigPath = ConfigPath;
			NewStream.ConfigRevision = ConfigRevision;
			NewStream.Order = Config.Order ?? StreamDocument.DefaultOrder;
			NewStream.NotificationChannel = Config.NotificationChannel;
			NewStream.NotificationChannelFilter = Config.NotificationChannelFilter;
			NewStream.TriageChannel = Config.TriageChannel;
			NewStream.DefaultPreflight = DefaultPreflight;
			NewStream.Tabs = Tabs;
			NewStream.AgentTypes = AgentTypes;
			NewStream.WorkspaceTypes = WorkspaceTypes;
			NewStream.Templates = TemplateRefs;
			NewStream.Acl = Acl;

			try
			{
				await Streams.InsertOneAsync(NewStream);
				return NewStream;
			}
			catch (MongoWriteException Ex)
			{
				if (Ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
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
		async Task<IStream?> TryReplaceAsync(IStream StreamInterface, ProjectId ProjectId, string ConfigPath, string ConfigRevision, StreamConfig Config, DefaultPreflight? DefaultPreflight, List<StreamTab> Tabs, Dictionary<string, AgentType>? AgentTypes, Dictionary<string, WorkspaceType>? WorkspaceTypes, Dictionary<TemplateRefId, TemplateRef>? TemplateRefs, Acl? Acl)
		{
			int Order = Config.Order ?? StreamDocument.DefaultOrder;

			StreamDocument Stream = (StreamDocument)StreamInterface;

			UpdateDefinitionBuilder<StreamDocument> UpdateBuilder = Builders<StreamDocument>.Update;

			List<UpdateDefinition<StreamDocument>> Updates = new List<UpdateDefinition<StreamDocument>>();
			Updates.Add(UpdateBuilder.Set(x => x.Name, Config.Name));
			Updates.Add(UpdateBuilder.Set(x => x.ProjectId, ProjectId));
			Updates.Add(UpdateBuilder.Set(x => x.ClusterName, Config.ClusterName));
			Updates.Add(UpdateBuilder.Set(x => x.ConfigPath, ConfigPath));
			Updates.Add(UpdateBuilder.Set(x => x.ConfigRevision, ConfigRevision));
			Updates.Add(UpdateBuilder.Set(x => x.Order, Order));
			Updates.Add(UpdateBuilder.Set(x => x.NotificationChannel, Config.NotificationChannel));
			Updates.Add(UpdateBuilder.Set(x => x.NotificationChannelFilter, Config.NotificationChannelFilter));
			Updates.Add(UpdateBuilder.Set(x => x.TriageChannel, Config.TriageChannel));
			Updates.Add(UpdateBuilder.Set(x => x.DefaultPreflight, DefaultPreflight));
			Updates.Add(UpdateBuilder.Set(x => x.Tabs, Tabs ?? new List<StreamTab>()));
			Updates.Add(UpdateBuilder.Set(x => x.AgentTypes, AgentTypes ?? new Dictionary<string, AgentType>()));
			Updates.Add(UpdateBuilder.Set(x => x.WorkspaceTypes, WorkspaceTypes ?? new Dictionary<string, WorkspaceType>()));
			Updates.Add(UpdateBuilder.Set(x => x.Templates, TemplateRefs ?? new Dictionary<TemplateRefId, TemplateRef>()));
			Updates.Add(UpdateBuilder.SetOrUnsetNullRef(x => x.Acl, Acl));
			Updates.Add(UpdateBuilder.Unset(x => x.Deleted));

			return await TryUpdateStreamAsync(Stream, UpdateBuilder.Combine(Updates));
		}

		/// <inheritdoc/>
		public async Task<IStream?> GetAsync(StreamId StreamId)
		{
			return await Streams.Find<StreamDocument>(x => x.Id == StreamId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IStreamPermissions?> GetPermissionsAsync(StreamId StreamId)
		{
			return await Streams.Find<StreamDocument>(x => x.Id == StreamId).Project<StreamPermissions>(StreamPermissions.Projection).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<IStream>> FindAllAsync()
		{
			List<StreamDocument> Results = await Streams.Find(Builders<StreamDocument>.Filter.Ne(x => x.Deleted, true)).ToListAsync();
			return Results.ConvertAll<IStream>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<IStream>> FindForProjectsAsync(ProjectId[] ProjectIds)
		{
			FilterDefinition<StreamDocument> Filter = Builders<StreamDocument>.Filter.In(x => x.ProjectId, ProjectIds) & Builders<StreamDocument>.Filter.Ne(x => x.Deleted, true);
			List<StreamDocument> Results = await Streams.Find(Filter).ToListAsync();
			return Results.ConvertAll<IStream>(x => x);
		}

		/// <inheritdoc/>
		public async Task<IStream?> TryUpdatePauseStateAsync(IStream StreamInterface, DateTime? NewPausedUntil, string? NewPauseComment)
		{
			StreamDocument Stream = (StreamDocument)StreamInterface;

			UpdateDefinitionBuilder<StreamDocument> UpdateBuilder = Builders<StreamDocument>.Update;

			List<UpdateDefinition<StreamDocument>> Updates = new List<UpdateDefinition<StreamDocument>>();
			Stream.PausedUntil = NewPausedUntil;
			Stream.PauseComment = NewPauseComment;
			Updates.Add(UpdateBuilder.Set(x => x.PausedUntil, NewPausedUntil));
			Updates.Add(UpdateBuilder.Set(x => x.PauseComment, NewPauseComment));

			return await TryUpdateStreamAsync(Stream, UpdateBuilder.Combine(Updates));
		}

		/// <inheritdoc/>
		public async Task<IStream?> TryUpdateScheduleTriggerAsync(IStream StreamInterface, TemplateRefId TemplateRefId, DateTimeOffset? LastTriggerTime, int? LastTriggerChange, List<JobId> NewActiveJobs)
		{
			StreamDocument Stream = (StreamDocument)StreamInterface;
			Schedule Schedule = Stream.Templates[TemplateRefId].Schedule!;

			// Build the updates. MongoDB driver cannot parse TemplateRefId in expression tree; need to specify field name explicitly
			List<UpdateDefinition<StreamDocument>> Updates = new List<UpdateDefinition<StreamDocument>>();
			if (LastTriggerTime.HasValue && LastTriggerTime.Value != Schedule.LastTriggerTime)
			{
				FieldDefinition<StreamDocument, DateTimeOffset> LastTriggerTimeField = $"{nameof(Stream.Templates)}.{TemplateRefId}.{nameof(Schedule)}.{nameof(Schedule.LastTriggerTime)}";
				Updates.Add(Builders<StreamDocument>.Update.Set(LastTriggerTimeField, LastTriggerTime.Value));
				Schedule.LastTriggerTime = LastTriggerTime.Value;
			}
			if (LastTriggerChange.HasValue && LastTriggerChange.Value > Schedule.LastTriggerChange)
			{
				FieldDefinition<StreamDocument, int> LastTriggerChangeField = $"{nameof(Stream.Templates)}.{TemplateRefId}.{nameof(Schedule)}.{nameof(Schedule.LastTriggerChange)}";
				Updates.Add(Builders<StreamDocument>.Update.Set(LastTriggerChangeField, LastTriggerChange.Value));
				Schedule.LastTriggerChange = LastTriggerChange.Value;
			}
			if (NewActiveJobs != null)
			{
				FieldDefinition<StreamDocument, List<JobId>> Field = $"{nameof(Stream.Templates)}.{TemplateRefId}.{nameof(Schedule)}.{nameof(Schedule.ActiveJobs)}";
				Updates.Add(Builders<StreamDocument>.Update.Set(Field, NewActiveJobs));
				Schedule.ActiveJobs = NewActiveJobs;
			}

			return (Updates.Count == 0)? StreamInterface : await TryUpdateStreamAsync(Stream, Builders<StreamDocument>.Update.Combine(Updates));
		}

		/// <summary>
		/// Update a stream
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="Update">The update definition</param>
		/// <returns>The updated document, or null the update failed</returns>
		private async Task<StreamDocument?> TryUpdateStreamAsync(StreamDocument Stream, UpdateDefinition<StreamDocument> Update)
		{
			FilterDefinition<StreamDocument> Filter = Builders<StreamDocument>.Filter.Expr(x => x.Id == Stream.Id && x.UpdateIndex == Stream.UpdateIndex);
			Update = Update.Set(x => x.UpdateIndex, Stream.UpdateIndex + 1);

			FindOneAndUpdateOptions<StreamDocument> Options = new FindOneAndUpdateOptions<StreamDocument> { ReturnDocument = ReturnDocument.After };
			return await Streams.FindOneAndUpdateAsync(Filter, Update, Options);
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(StreamId StreamId)
		{
			await Streams.UpdateOneAsync<StreamDocument>(x => x.Id == StreamId, Builders<StreamDocument>.Update.Set(x => x.Deleted, true).Inc(x => x.UpdateIndex, 1));
		}


		/// <summary>
		/// Checks the stream definition for consistency
		/// </summary>
		public static void Validate(StreamId StreamId, DefaultPreflight? DefaultPreflight, IReadOnlyDictionary<TemplateRefId, TemplateRef> Templates, IReadOnlyList<StreamTab> Tabs, IReadOnlyDictionary<string, AgentType> AgentTypes, IReadOnlyDictionary<string, WorkspaceType> WorkspaceTypes)
		{
			// Check the default preflight template is valid
			if (DefaultPreflight != null)
			{
				if (DefaultPreflight.TemplateRefId != null && !Templates.ContainsKey(DefaultPreflight.TemplateRefId.Value))
				{
					throw new InvalidStreamException($"Default preflight template was listed as '{DefaultPreflight.TemplateRefId.Value}', but no template was found by that name");
				}
			}

			// Check that all the templates are referenced by a tab
			HashSet<TemplateRefId> RemainingTemplates = new HashSet<TemplateRefId>(Templates.Keys);
			foreach (JobsTab JobsTab in Tabs.OfType<JobsTab>())
			{
				if (JobsTab.Templates != null)
				{
					RemainingTemplates.ExceptWith(JobsTab.Templates);
				}
			}
			if (RemainingTemplates.Count > 0)
			{
				throw new InvalidStreamException(String.Join("\n", RemainingTemplates.Select(x => $"Template '{x}' is not listed on any tab for {StreamId}")));
			}

			// Check that all the agent types reference valid workspace names
			foreach (KeyValuePair<string, AgentType> Pair in AgentTypes)
			{
				string? WorkspaceTypeName = Pair.Value.Workspace;
				if (WorkspaceTypeName != null && !WorkspaceTypes.ContainsKey(WorkspaceTypeName))
				{
					throw new InvalidStreamException($"Agent type '{Pair.Key}' references undefined workspace type '{Pair.Value.Workspace}' in {StreamId}");
				}
			}
		}
	}
}
