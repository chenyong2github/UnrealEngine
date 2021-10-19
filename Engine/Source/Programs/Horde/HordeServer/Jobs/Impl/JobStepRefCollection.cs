// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeServer.Utilities;

namespace HordeServer.Collections.Impl
{
	using LogId = ObjectId<ILogFile>;
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Collection of JobStepRef documents
	/// </summary>
	public class JobStepRefCollection : IJobStepRefCollection
	{
		class JobStepRef : IJobStepRef
		{
			[BsonId]
			public JobStepRefId Id { get; set; }

			public string JobName { get; set; } = "Unknown";

			public string Name { get; set; }

			public StreamId StreamId { get; set; }
			public TemplateRefId TemplateId { get; set; }
			public int Change { get; set; }
			public LogId? LogId { get; set; }
			public PoolId? PoolId { get; set; }
			public AgentId? AgentId { get; set; }
			public JobStepOutcome? Outcome { get; set; }
			public float BatchWaitTime { get; set; }
			public float BatchInitTime { get; set; }

			[BsonIgnoreIfNull]
			public DateTimeOffset? StartTime { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? StartTimeUtc { get; set; }

			[BsonIgnoreIfNull]
			public DateTimeOffset? FinishTime { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? FinishTimeUtc { get; set; }

			DateTime IJobStepRef.StartTimeUtc => StartTimeUtc ?? StartTime?.UtcDateTime ?? default;
			DateTime? IJobStepRef.FinishTimeUtc => FinishTimeUtc ?? FinishTime?.UtcDateTime;
			string IJobStepRef.NodeName => Name;

			public JobStepRef(JobStepRefId Id, string JobName, string NodeName, StreamId StreamId, TemplateRefId TemplateId, int Change, LogId? LogId, PoolId? PoolId, AgentId? AgentId, JobStepOutcome? Outcome, float BatchWaitTime, float BatchInitTime, DateTime StartTimeUtc, DateTime? FinishTimeUtc)
			{
				this.Id = Id;
				this.JobName = JobName;
				this.Name = NodeName;
				this.StreamId = StreamId;
				this.TemplateId = TemplateId;
				this.Change = Change;
				this.LogId = LogId;
				this.PoolId = PoolId;
				this.AgentId = AgentId;
				this.Outcome = Outcome;
				this.BatchWaitTime = BatchWaitTime;
				this.BatchInitTime = BatchInitTime;
				this.StartTime = StartTimeUtc;
				this.StartTimeUtc = StartTimeUtc;
				this.FinishTime = FinishTimeUtc;
				this.FinishTimeUtc = FinishTimeUtc;
			}
		}

		IMongoCollection<JobStepRef> JobStepRefs;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		public JobStepRefCollection(DatabaseService DatabaseService)
		{
			JobStepRefs = DatabaseService.GetCollection<JobStepRef>("JobStepRefs");

			if (!DatabaseService.ReadOnlyMode)
			{
				JobStepRefs.Indexes.CreateOne(new CreateIndexModel<JobStepRef>(Builders<JobStepRef>.IndexKeys.Ascending(x => x.StreamId).Ascending(x => x.TemplateId).Ascending(x => x.Name).Descending(x => x.Change)));
			}
		}

		/// <inheritdoc/>
		public async Task<IJobStepRef> InsertOrReplaceAsync(JobStepRefId Id, string JobName, string StepName, StreamId StreamId, TemplateRefId TemplateId, int Change, LogId? LogId, PoolId? PoolId, AgentId? AgentId, JobStepOutcome? Outcome, float WaitTime, float InitTime, DateTime StartTimeUtc, DateTime? FinishTimeUtc)
		{
			JobStepRef NewJobStepRef = new JobStepRef(Id, JobName, StepName, StreamId, TemplateId, Change, LogId, PoolId, AgentId, Outcome, WaitTime, InitTime, StartTimeUtc, FinishTimeUtc);
			await JobStepRefs.ReplaceOneAsync(Builders<JobStepRef>.Filter.Eq(x => x.Id, NewJobStepRef.Id), NewJobStepRef, new ReplaceOptions { IsUpsert = true });
			return NewJobStepRef;
		}

		/// <inheritdoc/>
		public async Task<List<IJobStepRef>> GetStepsForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int? Change, bool IncludeFailed, int Count)
		{
			// Find all the steps matching the given criteria
			FilterDefinitionBuilder<JobStepRef> FilterBuilder = Builders<JobStepRef>.Filter;

			FilterDefinition<JobStepRef> Filter = FilterDefinition<JobStepRef>.Empty;
			Filter &= FilterBuilder.Eq(x => x.StreamId, StreamId);
			Filter &= FilterBuilder.Eq(x => x.TemplateId, TemplateId);
			Filter &= FilterBuilder.Eq(x => x.Name, NodeName);
			if (Change != null)
			{
				Filter &= FilterBuilder.Lte(x => x.Change, Change.Value);
			}
			if (!IncludeFailed)
			{
				Filter &= FilterBuilder.Ne(x => x.Outcome, JobStepOutcome.Failure);
			}

			List<JobStepRef> Steps = await JobStepRefs.Find(Filter).SortByDescending(x => x.Change).ThenByDescending(x => x.StartTime).Limit(Count).ToListAsync();
			return Steps.ConvertAll<IJobStepRef>(x => x);
		}

		/// <inheritdoc/>
		public async Task<IJobStepRef?> GetPrevStepForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int Change)
		{
			return await JobStepRefs.Find(x => x.StreamId == StreamId && x.TemplateId == TemplateId && x.Name == NodeName && x.Change < Change && x.Outcome != null).SortByDescending(x => x.Change).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IJobStepRef?> GetNextStepForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int Change)
		{
			return await JobStepRefs.Find(x => x.StreamId == StreamId && x.TemplateId == TemplateId && x.Name == NodeName && x.Change > Change && x.Outcome != null).SortBy(x => x.Change).FirstOrDefaultAsync();
		}
	}
}
