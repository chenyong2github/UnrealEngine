// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Mvc;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Base class for event records
	/// </summary>
	[JsonKnownTypes(typeof(JobCompleteEventRecord), typeof(LabelCompleteEventRecord), typeof(StepCompleteEventRecord))]
	public class EventRecord
	{
	}

	/// <summary>
	/// Event that occurs when a job completes
	/// </summary>
	[JsonDiscriminator("Job")]
	public class JobCompleteEventRecord : EventRecord
	{
		/// <summary>
		/// The stream id
		/// </summary>
		[Required]
		public string StreamId { get; set; }

		/// <summary>
		/// The template id
		/// </summary>
		[Required]
		public string TemplateId { get; set; }

		/// <summary>
		/// Outcome of the job
		/// </summary>
		public LabelOutcome Outcome { get; set; } = LabelOutcome.Success;

		/// <summary>
		/// Default constructor
		/// </summary>
		private JobCompleteEventRecord()
		{
			StreamId = String.Empty;
			TemplateId = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="TemplateId">The template id</param>
		/// <param name="Outcome">Outcome of the job</param>
		public JobCompleteEventRecord(StreamId StreamId, TemplateRefId TemplateId, LabelOutcome Outcome)
		{
			this.StreamId = StreamId.ToString();
			this.TemplateId = TemplateId.ToString();
			this.Outcome = Outcome;
		}
	}

	/// <summary>
	/// Event that occurs when a label completes
	/// </summary>
	[JsonDiscriminator("Label")]
	public class LabelCompleteEventRecord : EventRecord
	{
		/// <summary>
		/// The stream id
		/// </summary>
		[Required]
		public string StreamId { get; set; }

		/// <summary>
		/// The template id
		/// </summary>
		[Required]
		public string TemplateId { get; set; }

		/// <summary>
		/// Name of the category for this label
		/// </summary>
		public string? CategoryName { get; set; }

		/// <summary>
		/// Name of the label to monitor
		/// </summary>
		[Required]
		public string LabelName { get; set; }

		/// <summary>
		/// Outcome of the job
		/// </summary>
		public LabelOutcome Outcome { get; set; } = LabelOutcome.Success;

		/// <summary>
		/// Default constructor
		/// </summary>
		private LabelCompleteEventRecord()
		{
			this.StreamId = String.Empty;
			this.TemplateId = String.Empty;
			this.LabelName = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="TemplateId">The template id</param>
		/// <param name="CategoryName">Name of the category</param>
		/// <param name="LabelName">The label name</param>
		/// <param name="Outcome">Outcome of the label</param>
		public LabelCompleteEventRecord(StreamId StreamId, TemplateRefId TemplateId, string? CategoryName, string LabelName, LabelOutcome Outcome)
		{
			this.StreamId = StreamId.ToString();
			this.TemplateId = TemplateId.ToString();
			this.CategoryName = CategoryName;
			this.LabelName = LabelName;
			this.Outcome = Outcome;
		}
	}

	/// <summary>
	/// Event that occurs when a step completes
	/// </summary>
	[JsonDiscriminator("Step")]
	public class StepCompleteEventRecord : EventRecord
	{
		/// <summary>
		/// The stream id
		/// </summary>
		[Required]
		public string StreamId { get; set; }

		/// <summary>
		/// The template id
		/// </summary>
		[Required]
		public string TemplateId { get; set; }

		/// <summary>
		/// Name of the step to monitor
		/// </summary>
		[Required]
		public string StepName { get; set; }

		/// <summary>
		/// Outcome of the job step
		/// </summary>
		public JobStepOutcome Outcome { get; set; } = JobStepOutcome.Success;

		/// <summary>
		/// Default constructor
		/// </summary>
		private StepCompleteEventRecord()
		{
			StreamId = String.Empty;
			TemplateId = String.Empty;
			StepName = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="TemplateId">The template id</param>
		/// <param name="StepName">The label name</param>
		/// <param name="Outcome">Outcome of the step</param>
		public StepCompleteEventRecord(StreamId StreamId, TemplateRefId TemplateId, string StepName, JobStepOutcome Outcome)
		{
			this.StreamId = StreamId.ToString();
			this.TemplateId = TemplateId.ToString();
			this.StepName = StepName;
			this.Outcome = Outcome;
		}
	}
}
