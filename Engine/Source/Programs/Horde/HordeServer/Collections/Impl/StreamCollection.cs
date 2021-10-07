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

			Dictionary<string, AgentType>? AgentTypes = null;
			if (Config.AgentTypes != null)
			{
				AgentTypes = Config.AgentTypes.Where(x => x.Value != null).ToDictionary(x => x.Key, x => new AgentType(x.Value!));
			}

			Dictionary<string, WorkspaceType>? WorkspaceTypes = null;
			if (Config.WorkspaceTypes != null)
			{
				WorkspaceTypes = Config.WorkspaceTypes.Where(x => x.Value != null).ToDictionary(x => x.Key, x => new WorkspaceType(x.Value!));
			}

			DefaultPreflight? DefaultPreflight = Config.DefaultPreflight?.ToModel();
			if (DefaultPreflight == null && Config.DefaultPreflightTemplate != null)
			{
				DefaultPreflight = new DefaultPreflight(new TemplateRefId(Config.DefaultPreflightTemplate), null);
			}

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
				ITemplate NewTemplate = await TemplateCollection.AddAsync(Request.Name, Request.Priority, Request.AllowPreflights, Request.InitialAgentType, Request.SubmitNewChange, Request.Counters, Request.Arguments, Request.Parameters.ConvertAll(x => x.ToModel()));

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
		public async Task<IStream?> TryCreateAsync(StreamId Id, ProjectId ProjectId, string ConfigPath, string ConfigRevision, StreamConfig Config, DefaultPreflight? DefaultPreflight, List<StreamTab>? Tabs, Dictionary<string, AgentType>? AgentTypes, Dictionary<string, WorkspaceType>? WorkspaceTypes, Dictionary<TemplateRefId, TemplateRef>? TemplateRefs, Acl? Acl)
		{
			StreamDocument NewStream = new StreamDocument(Id, Config.Name, ProjectId);
			NewStream.ClusterName = Config.ClusterName;
			NewStream.ConfigPath = ConfigPath;
			NewStream.ConfigRevision = ConfigRevision;
			NewStream.Order = Config.Order ?? StreamDocument.DefaultOrder;
			NewStream.NotificationChannel = Config.NotificationChannel;
			NewStream.NotificationChannelFilter = Config.NotificationChannelFilter;
			NewStream.TriageChannel = Config.TriageChannel;

			if (DefaultPreflight != null)
			{
				NewStream.DefaultPreflight = DefaultPreflight;
			}
			if (Tabs != null)
			{
				NewStream.Tabs = Tabs;
			}
			if (AgentTypes != null)
			{
				NewStream.AgentTypes = new Dictionary<string, AgentType>(AgentTypes, NewStream.AgentTypes.Comparer);
			}
			if (WorkspaceTypes != null)
			{
				NewStream.WorkspaceTypes = new Dictionary<string, WorkspaceType>(WorkspaceTypes, NewStream.WorkspaceTypes.Comparer);
			}
			if (TemplateRefs != null)
			{
				NewStream.Templates = TemplateRefs;
			}
			if (Acl != null)
			{
				NewStream.Acl = Acl;
			}
			NewStream.Validate();

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
		public async Task<IStream?> TryReplaceAsync(IStream StreamInterface, ProjectId ProjectId, string ConfigPath, string ConfigRevision, StreamConfig Config, DefaultPreflight? DefaultPreflight, List<StreamTab>? Tabs, Dictionary<string, AgentType>? AgentTypes, Dictionary<string, WorkspaceType>? WorkspaceTypes, Dictionary<TemplateRefId, TemplateRef>? TemplateRefs, Acl? Acl)
		{
			StreamDocument Stream = (StreamDocument)StreamInterface;

			UpdateDefinitionBuilder<StreamDocument> UpdateBuilder = Builders<StreamDocument>.Update;

			List<UpdateDefinition<StreamDocument>> Updates = new List<UpdateDefinition<StreamDocument>>();

			Stream.Name = Config.Name;
			Updates.Add(UpdateBuilder.Set(x => x.Name, Stream.Name));

			Stream.ProjectId = ProjectId;
			Updates.Add(UpdateBuilder.Set(x => x.ProjectId, Stream.ProjectId));

			Stream.ClusterName = Config.ClusterName;
			Updates.Add(UpdateBuilder.Set(x => x.ClusterName, Stream.ClusterName));

			Stream.ConfigPath = ConfigPath;
			Updates.Add(UpdateBuilder.Set(x => x.ConfigPath, Stream.ConfigPath));

			Stream.ConfigRevision = ConfigRevision;
			Updates.Add(UpdateBuilder.Set(x => x.ConfigRevision, Stream.ConfigRevision));

			Stream.Order = Config.Order ?? StreamDocument.DefaultOrder;
			Updates.Add(UpdateBuilder.Set(x => x.Order, Stream.Order));

			Stream.NotificationChannel = Config.NotificationChannel;
			Updates.Add(UpdateBuilder.Set(x => x.NotificationChannel, Stream.NotificationChannel));

			Stream.NotificationChannelFilter = Config.NotificationChannelFilter;
			Updates.Add(UpdateBuilder.Set(x => x.NotificationChannelFilter, Stream.NotificationChannelFilter));

			Stream.TriageChannel = Config.TriageChannel;
			Updates.Add(UpdateBuilder.Set(x => x.TriageChannel, Stream.TriageChannel));

			Stream.DefaultPreflight = DefaultPreflight;
			Updates.Add(UpdateBuilder.Set(x => x.DefaultPreflight, Stream.DefaultPreflight));
			
			Stream.Tabs = Tabs ?? new List<StreamTab>();
			Updates.Add(UpdateBuilder.Set(x => x.Tabs, Stream.Tabs));

			Stream.AgentTypes = AgentTypes ?? new Dictionary<string, AgentType>();
			Updates.Add(UpdateBuilder.Set(x => x.AgentTypes, Stream.AgentTypes));

			Stream.WorkspaceTypes = WorkspaceTypes ?? new Dictionary<string, WorkspaceType>();
			Updates.Add(UpdateBuilder.Set(x => x.WorkspaceTypes, Stream.WorkspaceTypes));

			Stream.Templates = TemplateRefs ?? new Dictionary<TemplateRefId, TemplateRef>();
			Updates.Add(UpdateBuilder.Set(x => x.Templates, Stream.Templates));

			Stream.Acl = Acl;
			Updates.Add(UpdateBuilder.SetOrUnsetNullRef(x => x.Acl, Acl));

			Stream.Deleted = false;
			Updates.Add(UpdateBuilder.Unset(x => x.Deleted));

			Stream.Validate();
			if(await TryUpdateStreamAsync(Stream, UpdateBuilder.Combine(Updates)))
			{
				return Stream;
			}
			else
			{
				return null;
			}
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
		public Task<bool> TryUpdatePauseStateAsync(IStream StreamInterface, DateTime? NewPausedUntil, string? NewPauseComment)
		{
			StreamDocument Stream = (StreamDocument)StreamInterface;

			UpdateDefinitionBuilder<StreamDocument> UpdateBuilder = Builders<StreamDocument>.Update;

			List<UpdateDefinition<StreamDocument>> Updates = new List<UpdateDefinition<StreamDocument>>();
			Stream.PausedUntil = NewPausedUntil;
			Stream.PauseComment = NewPauseComment;
			Updates.Add(UpdateBuilder.Set(x => x.PausedUntil, NewPausedUntil));
			Updates.Add(UpdateBuilder.Set(x => x.PauseComment, NewPauseComment));

			return TryUpdateStreamAsync(Stream, UpdateBuilder.Combine(Updates));
		}

		/// <inheritdoc/>
		public async Task<bool> TryUpdateScheduleTriggerAsync(IStream StreamInterface, TemplateRefId TemplateRefId, DateTimeOffset? LastTriggerTime, int? LastTriggerChange, List<ObjectId> NewActiveJobs)
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
				FieldDefinition<StreamDocument, List<ObjectId>> Field = $"{nameof(Stream.Templates)}.{TemplateRefId}.{nameof(Schedule)}.{nameof(Schedule.ActiveJobs)}";
				Updates.Add(Builders<StreamDocument>.Update.Set(Field, NewActiveJobs));
				Schedule.ActiveJobs = NewActiveJobs;
			}

			return Updates.Count == 0 || await TryUpdateStreamAsync(Stream, Builders<StreamDocument>.Update.Combine(Updates));
		}

		/// <summary>
		/// Update a stream
		/// </summary>
		/// <param name="Stream">The stream to update</param>
		/// <param name="Update">The update definition</param>
		/// <returns>True if the stream was updated, false otherwise</returns>
		private async Task<bool> TryUpdateStreamAsync(StreamDocument Stream, UpdateDefinition<StreamDocument> Update)
		{
			Stream.Validate();

			int InitialUpdateIndex = Stream.UpdateIndex++;

			UpdateResult Result = await Streams.UpdateOneAsync(x => x.Id == Stream.Id && x.UpdateIndex == InitialUpdateIndex, Update.Set(x => x.UpdateIndex, Stream.UpdateIndex));
			return Result.ModifiedCount > 0;
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(StreamId StreamId)
		{
			await Streams.UpdateOneAsync<StreamDocument>(x => x.Id == StreamId, Builders<StreamDocument>.Update.Set(x => x.Deleted, true).Inc(x => x.UpdateIndex, 1));
		}
	}
}
