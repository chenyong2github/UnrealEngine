using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using SubResourceId = HordeServer.Models.SubResourceId;

namespace HordeServer.Models
{
	/// <summary>
	/// Identifies a particular changelist and job that contributes to a span
	/// </summary>
	public interface IIssueStep
	{
		/// <summary>
		/// The span that this step belongs to
		/// </summary>
		public ObjectId SpanId { get; }

		/// <summary>
		/// The changelist number
		/// </summary>
		public int Change { get; }

		/// <summary>
		/// Name of the job
		/// </summary>
		public string JobName { get; }

		/// <summary>
		/// The unique job id
		/// </summary>
		public ObjectId JobId { get; }

		/// <summary>
		/// Unique id of the batch within the job
		/// </summary>
		public SubResourceId BatchId { get; }

		/// <summary>
		/// Unique id of the step within the job
		/// </summary>
		public SubResourceId StepId { get; }

		/// <summary>
		/// Time that the step started
		/// </summary>
		public DateTime StepTime { get; }

		/// <summary>
		/// Whether to notify suspects for this issue
		/// </summary>
		public bool NotifySuspects { get; }

		/// <summary>
		/// The log id for this step
		/// </summary>
		public ObjectId? LogId { get; }
	}
}
