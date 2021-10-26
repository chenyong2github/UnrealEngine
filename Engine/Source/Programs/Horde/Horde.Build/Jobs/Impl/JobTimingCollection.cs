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

namespace HordeServer.Collections.Impl
{
	using JobId = ObjectId<IJob>;

	/// <summary>
	/// Concrete implementation of IJobTimingCollection
	/// </summary>
	public class JobTimingCollection : IJobTimingCollection
	{
		/// <summary>
		/// Timing information for a particular step
		/// </summary>
		class JobStepTimingDocument : IJobStepTiming
		{
			public string Name { get; set; }
			public float? AverageWaitTime { get; set; }
			public float? AverageInitTime { get; set; }
			public float? AverageDuration { get; set; }

			[BsonConstructor]
			private JobStepTimingDocument()
			{
				this.Name = null!;
			}

			public JobStepTimingDocument(string Name, float? AverageWaitTime, float? AverageInitTime, float? AverageDuration)
			{
				this.Name = Name;
				this.AverageWaitTime = AverageWaitTime;
				this.AverageInitTime = AverageInitTime;
				this.AverageDuration = AverageDuration;
			}
		}

		/// <summary>
		/// Timing information for a particular job
		/// </summary>
		class JobTimingDocument : IJobTiming
		{
			public JobId Id { get; set; }
			public List<JobStepTimingDocument> Steps { get; set; } = new List<JobStepTimingDocument>();
			public int UpdateIndex { get; set; }

			[BsonIgnore]
			public Dictionary<string, IJobStepTiming>? NameToStep;

			public bool TryGetStepTiming(string Name, [NotNullWhen(true)] out IJobStepTiming? Timing)
			{
				if (NameToStep == null)
				{
					NameToStep = new Dictionary<string, IJobStepTiming>();
					foreach (JobStepTimingDocument Step in Steps)
					{
						if (NameToStep.ContainsKey(Step.Name))
						{
							Serilog.Log.Logger.Warning("Step {Name} appears twice in job timing document {Id}", Step.Name, Id);
						}
						NameToStep[Step.Name] = Step;
					}
				}
				return NameToStep.TryGetValue(Name, out Timing);
			}
		}

		/// <summary>
		/// Collection of timing documents
		/// </summary>
		IMongoCollection<JobTimingDocument> Collection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public JobTimingCollection(DatabaseService DatabaseService)
		{
			Collection = DatabaseService.GetCollection<JobTimingDocument>("JobTiming");
		}

		/// <inheritdoc/>
		public async Task<IJobTiming?> TryAddAsync(JobId JobId, List<JobStepTimingData> Steps)
		{
			JobTimingDocument JobTiming = new JobTimingDocument();
			JobTiming.Id = JobId;
			JobTiming.Steps.AddRange(Steps.Select(x => new JobStepTimingDocument(x.Name, x.AverageWaitTime, x.AverageInitTime, x.AverageDuration)));
			JobTiming.UpdateIndex = 1;

			try
			{
				await Collection.InsertOneAsync(JobTiming);
				return JobTiming;
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
		public async Task<IJobTiming?> TryGetAsync(JobId JobId)
		{
			return await Collection.Find(x => x.Id == JobId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IJobTiming?> TryAddStepsAsync(IJobTiming JobTiming, List<JobStepTimingData> Steps)
		{
			JobTimingDocument JobTimingDocument = (JobTimingDocument)JobTiming;
			List<JobStepTimingDocument> StepDocuments = Steps.ConvertAll(x => new JobStepTimingDocument(x.Name, x.AverageWaitTime, x.AverageInitTime, x.AverageDuration));

			FilterDefinition<JobTimingDocument> Filter = Builders<JobTimingDocument>.Filter.Expr(x => x.Id == JobTimingDocument.Id && x.UpdateIndex == JobTimingDocument.UpdateIndex);
			UpdateDefinition<JobTimingDocument> Update = Builders<JobTimingDocument>.Update.Set(x => x.UpdateIndex, JobTimingDocument.UpdateIndex + 1).PushEach(x => x.Steps, StepDocuments);

			UpdateResult Result = await Collection.UpdateOneAsync(Filter, Update);
			if (Result.ModifiedCount > 0)
			{
				JobTimingDocument.NameToStep = null;
				JobTimingDocument.Steps.AddRange(StepDocuments);
				JobTimingDocument.UpdateIndex++;
				return JobTimingDocument;
			}
			return null;
		}
	}
}
