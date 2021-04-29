using HordeCommon;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;

namespace HordeServer.Models
{
	/// <summary>
	/// Unique id struct for JobStepRef objects. Includes a job id, batch id, and step id to uniquely identify the step.
	/// </summary>
	[BsonSerializer(typeof(JobStepRefIdSerializer))]
	public struct JobStepRefId
	{
		/// <summary>
		/// The job id
		/// </summary>
		public ObjectId JobId { get; set; }

		/// <summary>
		/// The batch id within the job
		/// </summary>
		public SubResourceId BatchId { get; set; }

		/// <summary>
		/// The step id
		/// </summary>
		public SubResourceId StepId { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="JobId">The job id</param>
		/// <param name="BatchId">The batch id within the job</param>
		/// <param name="StepId">The step id</param>
		public JobStepRefId(ObjectId JobId, SubResourceId BatchId, SubResourceId StepId)
		{
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.StepId = StepId;
		}

		/// <summary>
		/// Parse a job step id from a string
		/// </summary>
		/// <param name="Text">Text to parse</param>
		/// <returns>The parsed id</returns>
		public static JobStepRefId Parse(string Text)
		{
			string[] Components = Text.Split(':');
			return new JobStepRefId(ObjectId.Parse(Components[0]), SubResourceId.Parse(Components[1]), SubResourceId.Parse(Components[2]));
		}

		/// <summary>
		/// Formats this id as a string
		/// </summary>
		/// <returns>Formatted id</returns>
		public override string ToString()
		{
			return $"{JobId}:{BatchId}:{StepId}";
		}
	}

	/// <summary>
	/// Serializer for JobStepRefId objects
	/// </summary>
	public sealed class JobStepRefIdSerializer : IBsonSerializer<JobStepRefId>
	{
		/// <inheritdoc/>
		public Type ValueType
		{
			get { return typeof(JobStepRefId); }
		}

		/// <inheritdoc/>
		void IBsonSerializer.Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, object Value)
		{
			Serialize(Context, Args, (JobStepRefId)Value);
		}

		/// <inheritdoc/>
		object IBsonSerializer.Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			return ((IBsonSerializer<JobStepRefId>)this).Deserialize(Context, Args);
		}

		/// <inheritdoc/>
		public JobStepRefId Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			return JobStepRefId.Parse(Context.Reader.ReadString());
		}

		/// <inheritdoc/>
		public void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, JobStepRefId Value)
		{
			Context.Writer.WriteString(((JobStepRefId)Value).ToString());
		}
	}

	/// <summary>
	/// Searchable reference to a jobstep
	/// </summary>
	public interface IJobStepRef
	{
		/// <summary>
		/// Globally unique identifier for the jobstep being referenced
		/// </summary>
		public JobStepRefId Id { get; }

		/// <summary>
		/// Name of the job
		/// </summary>
		public string JobName { get; }

		/// <summary>
		/// Name of the name
		/// </summary>
		public string NodeName { get; }

		/// <summary>
		/// Unique id of the stream containing the job
		/// </summary>
		public StreamId StreamId { get; }

		/// <summary>
		/// Template for the job being executed
		/// </summary>
		public TemplateRefId TemplateId { get; }

		/// <summary>
		/// The change number being built
		/// </summary>
		public int Change { get; }

		/// <summary>
		/// Log for this step
		/// </summary>
		public ObjectId? LogId { get; }

		/// <summary>
		/// The agent type
		/// </summary>
		public PoolId? PoolId { get; }

		/// <summary>
		/// The agent id
		/// </summary>
		public AgentId? AgentId { get; }

		/// <summary>
		/// Outcome of the step, once complete.
		/// </summary>
		public JobStepOutcome? Outcome { get; }

		/// <summary>
		/// Time taken for the batch containing this batch to start after it became ready
		/// </summary>
		public float BatchWaitTime { get; }

		/// <summary>
		/// Time taken for this batch to initialize
		/// </summary>
		public float BatchInitTime { get; }

		/// <summary>
		/// Time at which the step started.
		/// </summary>
		public DateTime StartTimeUtc { get; }

		/// <summary>
		/// Time at which the step finished.
		/// </summary>
		public DateTime? FinishTimeUtc { get; }
	}
}
