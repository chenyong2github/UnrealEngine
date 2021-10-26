// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.Runtime.Internal.Util;
using Amazon.S3.Model.Internal.MarshallTransformations;
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

namespace HordeServer.Collections
{
	using JobId = ObjectId<IJob>;

	/// <summary>
	/// Average timing information for a node
	/// </summary>
	public class JobStepTimingData
	{
		/// <summary>
		/// Name of the node
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Wait time before executing the group containing this node
		/// </summary>
		[BsonIgnoreIfNull]
		public float? AverageWaitTime { get; set; }

		/// <summary>
		/// Time taken for the group containing this node to initialize
		/// </summary>
		[BsonIgnoreIfNull]
		public float? AverageInitTime { get; set; }

		/// <summary>
		/// Time spent executing this node
		/// </summary>
		[BsonIgnoreIfNull]
		public float? AverageDuration { get; set; }

		/// <summary>
		/// Constructor for serialization
		/// </summary>
		[BsonConstructor]
		private JobStepTimingData()
		{
			this.Name = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the node</param>
		/// <param name="AverageWaitTime">Wait time before executing the group containing this node</param>
		/// <param name="AverageInitTime">Time taken for the group containing this node to initialize</param>
		/// <param name="AverageDuration">Time spent executing this node</param>
		public JobStepTimingData(string Name, float? AverageWaitTime, float? AverageInitTime, float? AverageDuration)
		{
			this.Name = Name;
			this.AverageWaitTime = AverageWaitTime;
			this.AverageInitTime = AverageInitTime;
			this.AverageDuration = AverageDuration;
		}
	}

	/// <summary>
	/// Interface for a collection of JobTiming documents
	/// </summary>
	public interface IJobTimingCollection
	{
		/// <summary>
		/// Add timing information for the given job
		/// </summary>
		/// <param name="JobId">The job id</param>
		/// <param name="Steps">List of timing info for each step</param>
		/// <returns>New timing document</returns>
		Task<IJobTiming?> TryAddAsync(JobId JobId, List<JobStepTimingData> Steps);

		/// <summary>
		/// Attempts to get the timing information for a particular job
		/// </summary>
		/// <param name="JobId">The unique job id</param>
		/// <returns>Timing info for the requested jbo</returns>
		Task<IJobTiming?> TryGetAsync(JobId JobId);

		/// <summary>
		/// Adds timing information for the particular job
		/// </summary>
		/// <param name="JobTiming">The current timing info for the job</param>
		/// <param name="Steps">List of steps to add</param>
		/// <returns>New timing document. Null if the document could not be updated.</returns>
		Task<IJobTiming?> TryAddStepsAsync(IJobTiming JobTiming, List<JobStepTimingData> Steps);
	}
}
