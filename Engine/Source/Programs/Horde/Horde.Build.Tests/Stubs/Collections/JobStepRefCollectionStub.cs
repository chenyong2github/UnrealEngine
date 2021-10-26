// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using Moq;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;

namespace HordeServerTests.Stubs.Collections
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	class JobStepRefStub : IJobStepRef
	{
		public JobStepRefId Id { get; set; }
		public string JobName { get; set; }
		public string NodeName { get; set; }
		public StreamId StreamId { get; set; }
		public TemplateRefId TemplateId { get; set; }
		public int Change { get; set; }
		public LogId? LogId { get; set; }
		public PoolId? PoolId { get; set; }
		public AgentId? AgentId { get; set; }
		public JobStepOutcome? Outcome { get; set; }

		public virtual float BatchWaitTime => throw new NotImplementedException();
		public virtual float BatchInitTime => throw new NotImplementedException();
		public virtual DateTime StartTimeUtc => throw new NotImplementedException();
		public virtual DateTime? FinishTimeUtc => throw new NotImplementedException();

		public JobStepRefStub(JobId JobId, SubResourceId BatchId, SubResourceId StepId, string JobName, string NodeName, StreamId StreamId, TemplateRefId TemplateId, int Change, JobStepOutcome? Outcome)
		{
			this.Id = new JobStepRefId(JobId, BatchId, StepId);
			this.JobName = JobName;
			this.NodeName = NodeName;
			this.StreamId = StreamId;
			this.TemplateId = TemplateId;
			this.Change = Change;
			this.Outcome = Outcome;
		}
	}

	class JobStepRefCollectionStub : IJobStepRefCollection
	{
		List<IJobStepRef> Refs = new List<IJobStepRef>();

		public void Add(IJobStepRef JobStepRef)
		{
			Refs.Add(JobStepRef);
		}

		public Task<IJobStepRef?> GetNextStepForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int Change)
		{
			IJobStepRef? NextRef = null;
			foreach (IJobStepRef Ref in Refs)
			{
				if (Ref.StreamId == StreamId && Ref.TemplateId == TemplateId && Ref.NodeName == NodeName && Ref.Change > Change)
				{
					if (NextRef == null || Ref.Change < NextRef.Change)
					{
						NextRef = Ref;
					}
				}
			}
			return Task.FromResult(NextRef);
		}

		public Task<IJobStepRef?> GetPrevStepForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int Change)
		{
			IJobStepRef? PrevRef = null;
			foreach (IJobStepRef Ref in Refs)
			{
				if (Ref.StreamId == StreamId && Ref.TemplateId == TemplateId && Ref.NodeName == NodeName && Ref.Change < Change)
				{
					if (PrevRef == null || Ref.Change > PrevRef.Change)
					{
						PrevRef = Ref;
					}
				}
			}
			return Task.FromResult(PrevRef);
		}

		Task<List<IJobStepRef>> IJobStepRefCollection.GetStepsForNodeAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int? Change, bool IncludeFailed, int Count)
		{
			throw new NotImplementedException();
		}

		Task<IJobStepRef> IJobStepRefCollection.InsertOrReplaceAsync(JobStepRefId Id, string JobName, string NodeName, StreamId StreamId, TemplateRefId TemplateId, int Change, LogId? LogId, PoolId? PoolId, AgentId? AgentId, JobStepOutcome? Outcome, float WaitTime, float InitTime, DateTime StartTime, DateTime? FinishTime)
		{
			throw new NotImplementedException();
		}
	}
}
