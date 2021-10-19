// Copyright Epic Games, Inc. All Rights Reserved.

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
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Collection of test data documents
	/// </summary>
	public class TestDataCollection : ITestDataCollection
	{
		/// <summary>
		/// Information about a test data document
		/// </summary>
		class TestDataDocument : ITestData
		{
			public ObjectId Id { get; set; }
			public StreamId StreamId { get; set; }
			public TemplateRefId TemplateRefId { get; set; }
			public JobId JobId { get; set; }
			public SubResourceId StepId { get; set; }
			public int Change { get; set; }
			public string Key { get; set; }
			public BsonDocument Data { get; set; }

			private TestDataDocument()
			{
				this.Key = String.Empty;
				this.Data = new BsonDocument();
			}

			public TestDataDocument(IJob Job, IJobStep JobStep, string Key, BsonDocument Value)
			{
				this.Id = ObjectId.GenerateNewId();
				this.StreamId = Job.StreamId;
				this.TemplateRefId = Job.TemplateId;
				this.JobId = Job.Id;
				this.StepId = JobStep.Id;
				this.Change = Job.Change;
				this.Key = Key;
				this.Data = Value;
			}
		}

		/// <summary>
		/// The stream collection
		/// </summary>
		IMongoCollection<TestDataDocument> TestDataDocuments;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		public TestDataCollection(DatabaseService DatabaseService)
		{
			TestDataDocuments = DatabaseService.GetCollection<TestDataDocument>("TestData");

			if (!DatabaseService.ReadOnlyMode)
			{
				TestDataDocuments.Indexes.CreateOne(new CreateIndexModel<TestDataDocument>(Builders<TestDataDocument>.IndexKeys.Ascending(x => x.StreamId).Ascending(x => x.Change).Ascending(x => x.Key)));
				TestDataDocuments.Indexes.CreateOne(new CreateIndexModel<TestDataDocument>(Builders<TestDataDocument>.IndexKeys.Ascending(x => x.JobId).Ascending(x => x.StepId).Ascending(x => x.Key), new CreateIndexOptions { Unique = true }));
			}
		}

		/// <inheritdoc/>
		public async Task<ITestData> AddAsync(IJob Job, IJobStep Step, string Key, BsonDocument Value)
		{
			TestDataDocument NewTestData = new TestDataDocument(Job, Step, Key, Value);
			await TestDataDocuments.InsertOneAsync(NewTestData);
			return NewTestData;
		}

		/// <inheritdoc/>
		public async Task<ITestData?> GetAsync(ObjectId Id)
		{
			return await TestDataDocuments.Find<TestDataDocument>(x => x.Id == Id).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ITestData>> FindAsync(StreamId? StreamId, int? MinChange, int? MaxChange, JobId? JobId, SubResourceId? StepId, string? Key = null, int Index = 0, int Count = 10)
		{
			FilterDefinition<TestDataDocument> Filter = FilterDefinition<TestDataDocument>.Empty;
			if (StreamId != null)
			{
				Filter &= Builders<TestDataDocument>.Filter.Eq(x => x.StreamId, StreamId.Value);
				if (MinChange != null)
				{
					Filter &= Builders<TestDataDocument>.Filter.Gte(x => x.Change, MinChange.Value);
				}
				if (MaxChange != null)
				{
					Filter &= Builders<TestDataDocument>.Filter.Lte(x => x.Change, MaxChange.Value);
				}
			}
			if (JobId != null)
			{
				Filter &= Builders<TestDataDocument>.Filter.Eq(x => x.JobId, JobId.Value);
				if (StepId != null)
				{
					Filter &= Builders<TestDataDocument>.Filter.Eq(x => x.StepId, StepId.Value);
				}
			}
			if (Key != null)
			{
				Filter &= Builders<TestDataDocument>.Filter.Eq(x => x.Key, Key);
			}

			SortDefinition<TestDataDocument> Sort = Builders<TestDataDocument>.Sort.Ascending(x => x.StreamId).Descending(x => x.Change);

			List<TestDataDocument> Results = await TestDataDocuments.Find(Filter).Sort(Sort).Skip(Index).Limit(Count).ToListAsync();
			return Results.ConvertAll<ITestData>(x => x);
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(ObjectId Id)
		{
			await TestDataDocuments.DeleteOneAsync<TestDataDocument>(x => x.Id == Id);
		}
	}
}
