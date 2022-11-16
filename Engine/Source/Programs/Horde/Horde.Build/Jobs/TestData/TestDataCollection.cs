// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using OpenTracing.Util;
using OpenTracing;
using Microsoft.CodeAnalysis;
using Microsoft.Extensions.Options;

namespace Horde.Build.Jobs.TestData
{
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;
	using TemplateId = StringId<ITemplateRef>;

	using TestId = ObjectId<ITest>;
	using TestSuiteId = ObjectId<ITestSuite>;
	using TestMetaId = ObjectId<ITestMeta>;
	using TestRefId = ObjectId<ITestDataRef>;

	/// <summary>
	/// Collection of test data documents
	/// </summary>
	public class TestDataCollection : ITestDataCollection
	{

		class TestMetaDocument : ITestMeta
		{
			[BsonRequired, BsonId]
			public TestMetaId Id { get; set; }

			[BsonRequired, BsonElement("p")]
			public List<string> Platforms { get; set; }
			IReadOnlyList<string> ITestMeta.Platforms => Platforms;

			[BsonRequired, BsonElement("c")]
			public List<string> Configurations { get; set; }
			IReadOnlyList<string> ITestMeta.Configurations => Configurations;

			[BsonRequired, BsonElement("bt")]
			public List<string> BuildTargets { get; set; }
			IReadOnlyList<string> ITestMeta.BuildTargets => BuildTargets;

			[BsonRequired, BsonElement("pn")]
			public string ProjectName { get; set; }

			[BsonRequired, BsonElement("r")]
			public string RHI { get; set; }

			private TestMetaDocument()
			{
				Platforms = new List<string>();
				Configurations = new List<string>();
				BuildTargets = new List<string>();

				RHI = String.Empty;
				ProjectName = String.Empty;
			}

			public TestMetaDocument(List<string> platforms, List<string> configurations, List<string> buildTargets, string projectName, string? rhi)
			{
				Id = TestMetaId.GenerateNewId();
				Platforms = platforms;
				Configurations = configurations;
				BuildTargets = buildTargets;
				ProjectName = projectName;
				RHI = rhi ?? "default";
			}
		}

		class TestDocument : ITest
		{
			[BsonRequired, BsonId]
			public TestId Id { get; set; }
			
			[BsonRequired, BsonElement("n")]
			public string Name { get; set; }

			[BsonIgnoreIfNull, BsonElement("sn")]
			public string? SuiteName { get; set; }

			[BsonIgnoreIfNull, BsonElement("dn")]
			public string? DisplayName { get; set; }

			[BsonRequired, BsonElement("m")]
			public List<TestMetaId> Metadata { get; set; }
			IReadOnlyList<TestMetaId> ITest.Metadata => Metadata;

			private TestDocument()
			{
				Name = String.Empty;
				Metadata = new List<TestMetaId>();
			}

			public TestDocument(string name, List<TestMetaId> meta, string? suiteName = null, string? displayName = null)
			{
				Id = TestId.GenerateNewId();				
				Name = name;
				Metadata = meta;
				SuiteName = suiteName;
				DisplayName = displayName;
			}
		}

		class TestSuiteDocument : ITestSuite
		{
			[BsonRequired, BsonId]
			public TestSuiteId Id { get; set; }
			
			[BsonRequired, BsonElement("n")]
			public string Name { get; set; }

			[BsonRequired, BsonElement("m")]
			public List<TestMetaId> Metadata { get; set; }
			IReadOnlyList<TestMetaId> ITestSuite.Metadata => Metadata;

			[BsonRequired, BsonElement("t")]
			public List<TestId> Tests { get; set; }
			IReadOnlyList<TestId> ITestSuite.Tests => Tests;

			private TestSuiteDocument()
			{
				Name = String.Empty;
				Tests = new List<TestId>();
				Metadata = new List<TestMetaId>();
			}

			public TestSuiteDocument(string name, List<TestMetaId> metaIds, List<TestId> tests)
			{
				Id = TestSuiteId.GenerateNewId();
				Name = name;
				Metadata = metaIds;
				Tests = tests;
			}
		}

		/// <summary>
		/// Stores the tests and suites running in a stream
		/// </summary>
		class TestStreamDocument : ITestStream
		{
			[BsonRequired, BsonId]
			public ObjectId Id { get; set; }

			[BsonRequired]
			public StreamId StreamId { get; set; }

			[BsonRequired]
			public List<TestId> Tests { get; set; } = new List<TestId>();
			IReadOnlyList<TestId> ITestStream.Tests => Tests;

			[BsonRequired]
			public List<TestSuiteId> TestSuites { get; set; } = new List<TestSuiteId>();
			IReadOnlyList<TestSuiteId> ITestStream.TestSuites => TestSuites;

			private TestStreamDocument()
			{

			}

			public TestStreamDocument(StreamId streamId)
			{
				Id = ObjectId.GenerateNewId();
				StreamId = streamId;
			}
		}

		class SuiteTestData : ISuiteTestData
		{
			[BsonRequired, BsonElement("tid")]
			public TestId TestId { get; set; }

			[BsonRequired, BsonElement("o")]
			public TestOutcome Outcome { get; set; }

			[BsonRequired, BsonElement("d")]
			public TimeSpan Duration { get; set; }

			[BsonRequired, BsonElement("uid")]
			public string UID { get; set; }

			public SuiteTestData(TestId id, TestOutcome outcome, TimeSpan duration, string uid)
			{
				TestId = id;
				Outcome = outcome;
				Duration = duration;
				UID = uid;
			}
		}

		class TestDataRefDocument : ITestDataRef
		{
			[BsonRequired, BsonId]
			public TestRefId Id { get; set; }

			[BsonRequired,BsonElement("sid")]
			public StreamId StreamId { get; set; }

			[BsonRequired, BsonElement("m")]
			public TestMetaId Metadata { get; set; }

			[BsonRequired, BsonElement("bcl")]
			public int BuildChangeList { get; set; }

			[BsonRequired, BsonElement("d")]
			public TimeSpan Duration { get; set; }

			// --- Single tests
			[BsonIgnoreIfNull, BsonElement("tid")]
			public TestId? TestId { get; set; }

			[BsonIgnoreIfNull, BsonElement("o")]
			public TestOutcome? Outcome { get; set; }
			
			// --- Suite tests			
			[BsonIgnoreIfNull, BsonElement("suid")]
			public TestSuiteId? SuiteId { get; set; }

			[BsonIgnoreIfNull, BsonElement("ssc")]
			public int? SuiteSkipCount { get; set; }

			[BsonIgnoreIfNull, BsonElement("swc")]
			public int? SuiteWarningCount { get; set; }

			[BsonIgnoreIfNull, BsonElement("sec")]
			public int? SuiteErrorCount { get; set; }

			private TestDataRefDocument()
			{

			}

			public TestDataRefDocument(StreamId streamId)
			{
				Id = TestRefId.GenerateNewId();
				StreamId = streamId;
			}
		}

		class TestDataDetailsDocument : ITestDataDetails
		{
			/// <summary>
			/// The corresponding test ref for these details
			/// </summary>
			[BsonRequired, BsonId]
			public TestRefId Id { get; set; }

			/// <summary>
			/// The full (raw) data for this test, which can be consumed by various clients
			/// </summary>
			[BsonRequired, BsonElement("tdid")]
			public List<ObjectId> TestDataIds { get; set; } = new List<ObjectId>();
			IReadOnlyList<ObjectId> ITestDataDetails.TestDataIds => TestDataIds;

			/// <summary>
			/// Suite test data
			/// </summary>
			[BsonIgnoreIfNull, BsonElement("sts")]
			public List<SuiteTestData>? SuiteTests { get; set; }
			IReadOnlyList<ISuiteTestData>? ITestDataDetails.SuiteTests => SuiteTests;

			private TestDataDetailsDocument()
			{

			}

			public TestDataDetailsDocument(TestRefId id, List<ObjectId> testDataIds, List<SuiteTestData>? suiteTests = null)
			{
				Id = id;
				TestDataIds = testDataIds;
				SuiteTests = suiteTests;
			}
		}

		/// <summary>
		/// Information about a test data document
		/// </summary>
		class TestDataDocument : ITestData
		{
			public ObjectId Id { get; set; }
			public StreamId StreamId { get; set; }
			public TemplateId TemplateRefId { get; set; }
			public JobId JobId { get; set; }
			public SubResourceId StepId { get; set; }
			public int Change { get; set; }
			public string Key { get; set; }
			public BsonDocument Data { get; set; }

			private TestDataDocument()
			{
				Key = String.Empty;
				Data = new BsonDocument();
			}

			public TestDataDocument(IJob job, IJobStep jobStep, string key, BsonDocument value)
			{
				Id = ObjectId.GenerateNewId();
				StreamId = job.StreamId;
				TemplateRefId = job.TemplateId;
				JobId = job.Id;
				StepId = jobStep.Id;
				Change = job.Change;
				Key = key;
				Data = value;
			}
		}

		/// <summary>
		/// The detailed test data collection
		/// </summary>
		readonly IMongoCollection<TestDataDocument> _testDataDocuments;

		/// <summary>
		/// Test meta collection
		/// </summary>
		readonly IMongoCollection<TestMetaDocument> _testMeta;

		/// <summary>
		/// Test collection
		/// </summary>
		readonly IMongoCollection<TestDocument> _tests;

		/// <summary>
		/// Test suite collection
		/// </summary>
		readonly IMongoCollection<TestSuiteDocument> _testSuites;

		/// <summary>
		/// Test data refs collection
		/// </summary>
		readonly IMongoCollection<TestDataRefDocument> _testRefs;

		/// <summary>
		/// Test data refs collection
		/// </summary>
		readonly IMongoCollection<TestDataDetailsDocument> _testDetails;
		/// <summary>
		/// Test streams collection
		/// </summary>
		readonly IMongoCollection<TestStreamDocument> _testStreams;

		readonly ILogger _logger;


		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService"></param>
		/// <param name="logger"></param>
		/// <param name="settings"></param>
		public TestDataCollection(MongoService mongoService, ILogger<TestDataCollection> logger, IOptionsMonitor<ServerSettings> settings)
		{
			_logger = logger;

			List<MongoIndex<TestDataDocument>> indexes = new List<MongoIndex<TestDataDocument>>();
			indexes.Add(keys => keys.Ascending(x => x.StreamId).Ascending(x => x.Change).Ascending(x => x.Key));
			indexes.Add(keys => keys.Ascending(x => x.JobId).Ascending(x => x.StepId).Ascending(x => x.Key), unique: true);
			_testDataDocuments = mongoService.GetCollection<TestDataDocument>("TestData", indexes);

			List<MongoIndex<TestMetaDocument>> metaIndexes = new List<MongoIndex<TestMetaDocument>>();
			metaIndexes.Add(keys => keys.Ascending(x => x.ProjectName).Ascending(x => x.Platforms).Ascending(x => x.Configurations).Ascending(x => x.BuildTargets).Ascending(x => x.RHI), unique: true);
			_testMeta = mongoService.GetCollection<TestMetaDocument>("TestData.MetaV2", metaIndexes);

			List<MongoIndex<TestDocument>> testIndexes = new List<MongoIndex<TestDocument>>();
			testIndexes.Add(keys => keys.Ascending(x => x.Name).Ascending(x => x.Metadata));
			_tests = mongoService.GetCollection<TestDocument>("TestData.TestsV2", testIndexes);

			List<MongoIndex<TestSuiteDocument>> testSuiteIndexes = new List<MongoIndex<TestSuiteDocument>>();
			testSuiteIndexes.Add(keys => keys.Ascending(x => x.Name).Ascending(x => x.Tests));
			_testSuites = mongoService.GetCollection<TestSuiteDocument>("TestData.TestSuitesV2", testSuiteIndexes);

			List<MongoIndex<TestDataRefDocument>> testRefIndexes = new List<MongoIndex<TestDataRefDocument>>();
			testRefIndexes.Add(keys => keys.Ascending(x => x.StreamId).Ascending(x => x.Metadata).Descending(x => x.BuildChangeList).Ascending(x => x.TestId).Ascending(x => x.SuiteId));
			_testRefs = mongoService.GetCollection<TestDataRefDocument>("TestData.TestRefsV2", testRefIndexes);

			_testDetails = mongoService.GetCollection<TestDataDetailsDocument>("TestData.TestDetailsV2");

			List<MongoIndex<TestStreamDocument>> streamIndexes = new List<MongoIndex<TestStreamDocument>>();
			streamIndexes.Add(keys => keys.Ascending(x => x.StreamId), unique: true);
			_testStreams = mongoService.GetCollection<TestStreamDocument>("TestData.TestStreamsV2", streamIndexes);

		}

		/// <summary>
		/// Gets test data refs
		/// </summary>
		/// <param name="streamIds"></param>
		/// <param name="metaIds"></param>
		/// <param name="testIds"></param>
		/// <param name="suiteIds"></param>
		/// <param name="minCreateTime"></param>
		/// <param name="maxCreateTime"></param>
		/// <param name="minChange"></param>
		/// <param name="maxChange"></param>
		/// <returns></returns>
		public async Task<List<ITestDataRef>> FindTestRefs(StreamId[] streamIds, TestMetaId[]? metaIds = null, TestId[]? testIds = null, TestSuiteId[]? suiteIds = null, DateTime? minCreateTime = null, DateTime? maxCreateTime = null, int? minChange = null, int? maxChange = null)
		{

			FilterDefinition<TestDataRefDocument> filter = FilterDefinition<TestDataRefDocument>.Empty;
			FilterDefinitionBuilder<TestDataRefDocument> filterBuilder = Builders<TestDataRefDocument>.Filter;

			filter &= Builders<TestDataRefDocument>.Filter.In(x => x.StreamId, streamIds);

			if (minCreateTime != null)
			{
				TestRefId minTime = TestRefId.GenerateNewId(minCreateTime.Value);
				filter &= filterBuilder.Gte(x => x.Id!, minTime);

			}
			if (maxCreateTime != null)
			{
				TestRefId maxTime = TestRefId.GenerateNewId(maxCreateTime.Value);
				filter &= filterBuilder.Lte(x => x.Id!, maxTime);
			}

			if (metaIds != null && metaIds.Length > 0)
			{
				filter &= filterBuilder.In(x => x.Metadata, metaIds);
			}

			if ((testIds != null && testIds.Length> 0) && (suiteIds != null && suiteIds.Length > 0))
			{
				filter &= filterBuilder.Or(filterBuilder.And(filterBuilder.Ne(x => x.TestId, null), filterBuilder.In(x => (TestId)x.TestId!, testIds)),
										   filterBuilder.And(filterBuilder.Ne(x => x.SuiteId, null), filterBuilder.In(x => (TestSuiteId)x.SuiteId!, suiteIds)));
			}
			else
			{
				if (testIds != null && testIds.Length > 0)
				{
					filter &= filterBuilder.And(filterBuilder.Ne(x => x.TestId, null), filterBuilder.In(x => (TestId)x.TestId!, testIds));
				}

				if (suiteIds != null && suiteIds.Length > 0)
				{
					filter &= filterBuilder.And(filterBuilder.Ne(x => x.SuiteId, null), filterBuilder.In(x => (TestSuiteId)x.SuiteId!, suiteIds));
				}
			}

			if (minChange != null)
			{
				filter &= filterBuilder.Gte(x => x.BuildChangeList, minChange);
			}
			if (maxChange != null)
			{
				filter &= filterBuilder.Lte(x => x.BuildChangeList, maxChange);
			}

			List<TestDataRefDocument> results;
			using (IScope scope = GlobalTracer.Instance.BuildSpan("TestData.FindTestRefs").StartActive())
			{
				results = await _testRefs.Find(filter).ToListAsync();
			}

			return results.ConvertAll<ITestDataRef>(x => x);

		}

		/// <inheritdoc/>
		public async Task<List<ITestData>> AddAsync(IJob job, IJobStep step, (string key, BsonDocument value)[] data)
		{
			// detailed test data
			List<TestDataDocument> documents = new List<TestDataDocument>();
			for (int i = 0; i < data.Length; i++)
			{
				(string key, BsonDocument document) = data[i];
				documents.Add(new TestDataDocument(job, step, key, document));
				
			}

			await _testDataDocuments.InsertManyAsync(documents);

			try
			{
				await AddTestReportData(job, step, documents);
			}
			catch (Exception ex)
			{
				_logger.LogWarning(ex, "Exception while adding test data  report, jobId: {JobId} stepId: {StepId}", job.Id, step.Id);
			}

			return documents.ConvertAll<ITestData>(x => x); ;
		}

		/// <inheritdoc/>
		public async Task<ITestData?> GetAsync(ObjectId id)
		{
			return await _testDataDocuments.Find<TestDataDocument>(x => x.Id == id).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ITestData>> FindAsync(StreamId? streamId, int? minChange, int? maxChange, JobId? jobId, SubResourceId? stepId, string? key = null, int index = 0, int count = 10)
		{
			FilterDefinition<TestDataDocument> filter = FilterDefinition<TestDataDocument>.Empty;
			if (streamId != null)
			{
				filter &= Builders<TestDataDocument>.Filter.Eq(x => x.StreamId, streamId.Value);
				if (minChange != null)
				{
					filter &= Builders<TestDataDocument>.Filter.Gte(x => x.Change, minChange.Value);
				}
				if (maxChange != null)
				{
					filter &= Builders<TestDataDocument>.Filter.Lte(x => x.Change, maxChange.Value);
				}
			}
			if (jobId != null)
			{
				filter &= Builders<TestDataDocument>.Filter.Eq(x => x.JobId, jobId.Value);
				if (stepId != null)
				{
					filter &= Builders<TestDataDocument>.Filter.Eq(x => x.StepId, stepId.Value);
				}
			}
			if (key != null)
			{
				filter &= Builders<TestDataDocument>.Filter.Eq(x => x.Key, key);
			}

			SortDefinition<TestDataDocument> sort = Builders<TestDataDocument>.Sort.Ascending(x => x.StreamId).Descending(x => x.Change);

			List<TestDataDocument> results = await _testDataDocuments.Find(filter).Sort(sort).Skip(index).Limit(count).ToListAsync();
			return results.ConvertAll<ITestData>(x => x);
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(ObjectId id)
		{
			await _testDataDocuments.DeleteOneAsync<TestDataDocument>(x => x.Id == id);
		}

		/// <inheritdoc/>
		public async Task<List<ITestMeta>> FindTestMeta(string[]? projectNames = null, string[]? platforms = null, string[]? configurations = null, string[]? buildTargets = null, string? rhi = null, TestMetaId[]? metaIds = null)
		{
			FilterDefinitionBuilder<TestMetaDocument> filterBuilder = Builders<TestMetaDocument>.Filter;
			FilterDefinition<TestMetaDocument> filter = FilterDefinition<TestMetaDocument>.Empty;

			if (metaIds != null && metaIds.Length > 0)
			{
				if (metaIds.Length == 1)
				{
					filter &= filterBuilder.Eq(x => x.Id, metaIds[0]);
				}
				else
				{
					filter &= filterBuilder.In(x => x.Id, metaIds);
				}
			}

			if (projectNames != null && projectNames.Length > 0)
			{
				if (projectNames.Length == 1)
				{
					filter &= filterBuilder.Eq(x => x.ProjectName, projectNames[0]);
				}
				else
				{
					filter &= filterBuilder.In(x => x.ProjectName, projectNames);
				}
			}

			if (platforms != null && platforms.Length > 0)
			{
				if (platforms.Length == 1)
				{
					filter &= filterBuilder.AnyEq(x => x.Platforms, platforms[0]);
				}
				else
				{
					filter &= filterBuilder.AnyIn(x => x.Platforms, platforms);
				}
			}

			if (configurations != null && configurations.Length > 0)
			{
				if (configurations.Length == 1)
				{
					filter &= filterBuilder.AnyEq(x => x.Configurations, configurations[0]);
				}
				else
				{
					filter &= filterBuilder.AnyIn(x => x.Configurations, configurations);
				}
			}

			if (buildTargets != null && buildTargets.Length > 0)
			{
				if (buildTargets.Length == 1)
				{
					filter &= filterBuilder.AnyEq(x => x.BuildTargets, buildTargets[0]);
				}
				else
				{
					filter &= filterBuilder.AnyIn(x => x.BuildTargets, buildTargets);
				}
			}

			if (rhi != null)
			{
				filter &= filterBuilder.Eq(x => x.RHI, rhi);
			}

			List<TestMetaDocument> result = await _testMeta.Find(filter).ToListAsync();

			return result.ConvertAll<ITestMeta>(x => x);			
		}

		/// <inheritdoc/>
		public async Task<List<ITest>> FindTests(TestId[] testIds)
		{
			List<TestDocument> results = await _tests.Find(Builders<TestDocument>.Filter.In(x => x.Id, testIds)).ToListAsync();
			return results.ConvertAll<ITest>(x => x);

		}

		/// <inheritdoc/>
		public async Task<List<ITestSuite>> FindTestSuites(TestSuiteId[] suiteIds)
		{
			List<TestSuiteDocument> results = await _testSuites.Find(Builders<TestSuiteDocument>.Filter.In(x => x.Id, suiteIds)).ToListAsync();
			return results.ConvertAll<ITestSuite>(x => x);

		}

		private async Task<List<TestDocument>> FindTests(bool? suiteTests = null, string[]? projectNames = null, string[]? testNames = null, string[]? suiteNames = null, TestMetaId[]? metaIds = null, TestId[]? testIds = null)
		{
			List<ITestMeta> metaData = new List<ITestMeta>();

			if ((projectNames != null && projectNames.Length > 0) || (metaIds != null && metaIds.Length > 0))
			{
				metaData = await FindTestMeta(projectNames, metaIds: metaIds);
			}
			
			TestMetaId[] queryIds = metaData.Select(x => x.Id).ToArray();

			FilterDefinitionBuilder<TestDocument> filterBuilder = Builders<TestDocument>.Filter;
			FilterDefinition<TestDocument> filter = FilterDefinition<TestDocument>.Empty;

			if (queryIds.Length > 0)
			{
				if (queryIds.Length == 1)
				{
					filter &= filterBuilder.AnyEq(x => x.Metadata, queryIds[0]);
				}
				else
				{
					filter &= filterBuilder.AnyIn(x => x.Metadata, queryIds);
				}
			}

			if (testNames != null)
			{
				if (testNames.Length == 1)
				{
					filter &= filterBuilder.Eq(x => x.Name, testNames[0]);
				}
				else
				{
					filter &= filterBuilder.In(x => x.Name, testNames);
				}
			}

			if (testIds != null)
			{
				if (testIds.Length == 1)
				{
					filter &= filterBuilder.Eq(x => x.Id, testIds[0]);
				}
				else
				{
					filter &= filterBuilder.In(x => x.Id, testIds);
				}
			}			
			if (suiteTests == false)
			{
				filter &= filterBuilder.Eq(x => x.SuiteName, null);
			}
			else if (suiteTests == true)
			{
				filter &= filterBuilder.Ne(x => x.SuiteName, null);

				if (suiteNames != null)
				{
					if (suiteNames.Length == 1)
					{
						filter &= filterBuilder.Eq(x => x.SuiteName, suiteNames[0]);
					}
					else
					{
						filter &= filterBuilder.In(x => x.SuiteName, suiteNames);
					}
				}
			}

			return await _tests.Find(filter).ToListAsync();
		}

		private async Task<List<TestSuiteDocument>> FindTestSuites(string[] suiteNames, TestMetaId[] metaIds)
		{

			FilterDefinitionBuilder<TestSuiteDocument> filterBuilder = Builders<TestSuiteDocument>.Filter;
			FilterDefinition<TestSuiteDocument> filter = FilterDefinition<TestSuiteDocument>.Empty;

			if (suiteNames.Length == 1)
			{
				filter &= filterBuilder.Eq(x => x.Name, suiteNames[0]);
			}
			else
			{
				filter &= filterBuilder.In(x => x.Name, suiteNames);
			}

			if (metaIds.Length == 1)
			{
				filter &= filterBuilder.AnyEq(x => x.Metadata, metaIds[0]);
			}
			else
			{
				filter &= filterBuilder.AnyIn(x => x.Metadata, metaIds);
			}


			return await _testSuites.Find(filter).ToListAsync();
		}

		/// <inheritdoc/>
		public async Task<List<ITestStream>> FindTestStreams(StreamId[] streamIds)
		{
			List<TestStreamDocument> results  = await _testStreams.Find(Builders<TestStreamDocument>.Filter.In(x => x.StreamId, streamIds)).ToListAsync();
			return results.ConvertAll<ITestStream>(x => x);
		}

		async Task AddTestRef(TestDataRefDocument testRef)
		{
			StreamId streamId = testRef.StreamId;

			List<TestStreamDocument> streams = await _testStreams.Find(Builders<TestStreamDocument>.Filter.Eq(x => x.StreamId, streamId)).ToListAsync();

			if (streams.Count == 0)
			{
				TestStreamDocument streamDoc = new TestStreamDocument(streamId);
				if (testRef.TestId != null)
				{
					streamDoc.Tests.Add(testRef.TestId.Value);
				}

				if (testRef.SuiteId != null)
				{
					streamDoc.TestSuites.Add(testRef.SuiteId.Value);
				}

				await _testStreams.InsertOneAsync(streamDoc);
			}
			else
			{
				UpdateDefinitionBuilder<TestStreamDocument> updateBuilder = Builders<TestStreamDocument>.Update;
				List<UpdateDefinition<TestStreamDocument>> updates = new List<UpdateDefinition<TestStreamDocument>>();

				TestStreamDocument streamDoc = streams[0];

				if (testRef.TestId != null)
				{
					if (!streamDoc.Tests.Contains(testRef.TestId.Value))
					{
						streamDoc.Tests.Add(testRef.TestId.Value);
						updates.Add(updateBuilder.Set(x => x.Tests, streamDoc.Tests));
					}					
				}

				if (testRef.SuiteId != null)
				{
					if (!streamDoc.TestSuites.Contains(testRef.SuiteId.Value))
					{
						streamDoc.TestSuites.Add(testRef.SuiteId.Value);
						updates.Add(updateBuilder.Set(x => x.TestSuites, streamDoc.TestSuites));
					}
				}
				if (updates.Count > 0)
				{
					FilterDefinitionBuilder<TestStreamDocument> ebuilder = Builders<TestStreamDocument>.Filter;
					FilterDefinition<TestStreamDocument> efilter = ebuilder.Eq(x => x.StreamId, streamDoc.StreamId);
					await _testStreams.UpdateOneAsync(efilter, updateBuilder.Combine(updates));

				}
			}
			await _testRefs.InsertOneAsync(testRef);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="metaData"></param>
		/// <param name="job"></param>
		/// <param name="step"></param>
		/// <param name="testName"></param>
		/// <returns></returns>
		private async Task<TestMetaId?> AddTestMeta(Dictionary<string, string> metaData, IJob job, IJobStep step, string testName)
		{
			string? platform;
			if (!metaData.TryGetValue("Platform", out platform))
			{
				_logger.LogWarning("Test {TestName} is missing platform metadata, jobId: {JobId} stepId: {StepId}", testName, job.Id, step.Id);
				return null;
			}

			List<string> platforms = platform.Split('+', StringSplitOptions.TrimEntries).OrderBy(p => p).ToList();

			string? buildTarget;
			if (!metaData.TryGetValue("BuildTarget", out buildTarget))
			{
				_logger.LogWarning("Test {TestName} is missing build target metadata, jobId: {JobId} stepId: {StepId}", testName, job.Id, step.Id);
				return null;
			}

			List<string> buildTargets = buildTarget.Split('+', StringSplitOptions.TrimEntries).OrderBy(b => b).ToList();

			string? configuration;
			if (!metaData.TryGetValue("Configuration", out configuration))
			{
				_logger.LogWarning("Test {TestName} is missing configuration metadata, jobId: {JobId} stepId: {StepId}", testName, job.Id, step.Id);
				return null;
			}

			List<string> configurations = configuration.Split('+', StringSplitOptions.TrimEntries).OrderBy(c => c).ToList();

			string? projectName;
			if (!metaData.TryGetValue("Project", out projectName))
			{
				_logger.LogWarning("Test {TestName} is missing project metadata, jobId: {JobId} stepId: {StepId}", testName, job.Id, step.Id);
				return null;
			}

			string? rhi;
			metaData.TryGetValue("RHI", out rhi);
			rhi = rhi ?? "default";

			TestMetaDocument meta = new TestMetaDocument(platforms, configurations, buildTargets, projectName, rhi);

			FilterDefinitionBuilder<TestMetaDocument> filterBuilder = Builders<TestMetaDocument>.Filter;
			FilterDefinition<TestMetaDocument> filter = filterBuilder.Eq(x => x.ProjectName, meta.ProjectName);
			filter &= filterBuilder.Eq(x => x.RHI, meta.RHI);
			filter &= filterBuilder.Eq(x => x.Platforms, meta.Platforms);
			filter &= filterBuilder.Eq(x => x.BuildTargets, meta.BuildTargets);
			filter &= filterBuilder.Eq(x => x.Configurations, meta.Configurations);

			TestMetaDocument? result = await _testMeta.Find(filter).FirstOrDefaultAsync();

			if (result == null)
			{
				result = meta;
				await _testMeta.InsertOneAsync(result);
			}
			
			return result.Id;

		}

		async Task<TestDocument?> AddOrUpdateTest(TestMetaId metaId, string testName, string? suiteName = null,  string? displayName = null)
		{

			List<TestDocument> tests;

			if (suiteName != null)
			{
				tests = await FindTests(true, metaIds: new TestMetaId[] { metaId }, testNames: new string[] { testName }, suiteNames: new string[] {suiteName});
			}
			else
			{
				tests = await FindTests(false, metaIds: new TestMetaId[] { metaId }, testNames: new string[] { testName });
			}

			if (tests.Count > 1)
			{
				throw new Exception($"Duplicate tests found for MetaId: {metaId}, TestName: {testName}, SuiteName: {suiteName}, Count: {tests.Count}");
			}

			TestDocument? test;

			if (tests.Count == 0)
			{
				test = new TestDocument( testName, new List<TestMetaId>() { metaId }, suiteName, displayName);
				await _tests.InsertOneAsync(test);
			}
			else
			{
				test = tests[0];

				if (!test.Metadata.Contains(metaId))
				{
					test.Metadata.Add(metaId);

					FilterDefinitionBuilder<TestDocument> ebuilder = Builders<TestDocument>.Filter;
					FilterDefinition<TestDocument> efilter = ebuilder.Eq(x => x.Id, test.Id);

					UpdateDefinitionBuilder<TestDocument> updateBuilder = Builders<TestDocument>.Update;
					UpdateDefinition<TestDocument> update = updateBuilder.Set(x => x.Metadata, test.Metadata);
					await _tests.UpdateOneAsync(efilter, update);
				}
			}

			return test;
		}

		private async Task<TestSuiteDocument?> AddOrUpdateTestSuite(string suiteName, TestMetaId metaId, List<TestId> testIds)
		{

			TestSuiteDocument? suite;

			List<TestSuiteDocument> suiteTests = await FindTestSuites(suiteNames: new string[] { suiteName }, metaIds: new TestMetaId[] {metaId});

			if (suiteTests.Count > 1)
			{
				throw new Exception($"Duplicate tests found for MetaId: {metaId}, SuiteName: {suiteName}, Count: {suiteTests.Count}");
			}


			if (suiteTests.Count == 0)
			{
				suite = new TestSuiteDocument(suiteName, new List<TestMetaId> { metaId }, testIds);
				await _testSuites.InsertOneAsync(suite);
			}
			else
			{
				suite = suiteTests[0];

				List<TestId> newIds = suite.Tests.Union(testIds).ToList();

				if (newIds.Count != suite.Tests.Count || !suite.Metadata.Contains(metaId))
				{
					List<UpdateDefinition<TestSuiteDocument>> updates = new List<UpdateDefinition<TestSuiteDocument>>();
					FilterDefinitionBuilder<TestSuiteDocument> ebuilder = Builders<TestSuiteDocument>.Filter;
					FilterDefinition<TestSuiteDocument> efilter = ebuilder.Eq(x => x.Id, suite.Id);

					UpdateDefinitionBuilder<TestSuiteDocument> updateBuilder = Builders<TestSuiteDocument>.Update;

					if (newIds.Count != suite.Tests.Count)
					{
						suite.Tests = newIds;
						updates.Add(updateBuilder.Set(x => x.Tests, suite.Tests));
					}

					if (!suite.Metadata.Contains(metaId))
					{
						suite.Metadata.Add(metaId);
						updates.Add(updateBuilder.Set(x => x.Metadata, suite.Metadata));
					}

					await _testSuites.UpdateOneAsync(efilter, updateBuilder.Combine(updates));
				}

			}

			return suite;
		}

		async Task AddTestReportData(IJob job, IJobStep step, List<TestDataDocument> documents)
		{
			List<AutomatedTestSessionData> sessions = new List<AutomatedTestSessionData>();
			List<UnrealAutomatedTestData> tests = new List<UnrealAutomatedTestData>();

			// Get test document version
			int version = 0;
			foreach (TestDataDocument item in documents)
			{
				BsonDocument testData = item.Data;
				version = testData.GetValue("Version", new BsonInt32(0)).AsInt32;
				if (version > 0)
				{
					break;
				}
			}

			if (version < 1)
			{
				_logger.LogWarning("Test data does not have version and needs to be updated in stream for job {JobId} step {StepId}", job.Id, step.Id);
				return;
			}

			foreach (TestDataDocument item in documents)
			{
				BsonDocument testData = item.Data;

				BsonValue? value;
				if (!testData.TryGetPropertyValue("Type", BsonType.String, out value))
				{
					continue;
				}

				string type = value.AsString;				

				if (type == "Simple Report")
				{
					await AddSimpleReportData(job, step, item.Id, testData);
					continue;
				}

				if (type == "Automated Test Session")
				{
					sessions.Add(BsonSerializer.Deserialize<AutomatedTestSessionData>(testData));
				}

				if (type == "Unreal Automated Tests")
				{
					tests.Add(BsonSerializer.Deserialize<UnrealAutomatedTestData>(testData));
				}
			}

			if (sessions.Count > 0)
			{
				await AddTestSessionReportData(job, step, documents, sessions, tests);
			}
		}

		// --- (Minimal) Parsing of Automation framework tests, geared towards generating indexes for aggregate queries

		class AutomatedTestSessionData
		{
			public class SessionTest
			{
				public string Name { get; set; } = String.Empty;
				public string TestUID { get; set; } = String.Empty;
				public string Suite { get; set; } = String.Empty;
				public string State { get; set; } = String.Empty;
				public List<string> DeviceAppInstanceName { get; set; } = new List<string>();
				public int ErrorCount { get; set; } = 0;
				public int WarningCount { get; set; } = 0;
				public double TimeElapseSec { get; set; } = 0;
			}

			public class TestSessionInfoType
			{
				public double TimeElapseSec { get; set; } = 0;
				public Dictionary<string, SessionTest> Tests { get; set; } = new Dictionary<string, SessionTest>();
				public string TestResultsTestDataUID { get; set; } = String.Empty;
			}

			public string Type { get; set; } = String.Empty;
			public string Name { get; set; } = String.Empty;

			public TestSessionInfoType? TestSessionInfo { get; set; }
			public Dictionary<string, string> Metadata { get; set; } = new Dictionary<string, string>();

		}

		class UnrealAutomatedTestData
		{
			public class UnrealTest
			{
				public string TestDisplayName { get; set; } = String.Empty;
				public string FullTestPath { get; set; } = String.Empty;
				public string State { get; set; } = String.Empty;
				public string DeviceInstance { get; set; } = String.Empty;
				public int Errors { get; set; } = 0;
				public int Warnings { get; set; } = 0;				
			}

			public string Type { get; set; } = String.Empty;
			public double TotalDurationSeconds { get; set; } = 0;
			public List<UnrealTest> Tests { get; set; } = new List<UnrealTest>();
			public Dictionary<string, string> Metadata { get; set; } = new Dictionary<string, string>();

		}

		// Simple Test Reports

		class TestRole
		{
			public string Type { get; set; } = String.Empty;
			public string Platform { get; set; } = String.Empty;
			public string Configuration { get; set; } = String.Empty;
		}

		class SimpleTestData
		{			
			public string TestName { get; set; } = String.Empty;
			public string Description { get; set; } = String.Empty;
			public string ReportCreatedOn { get; set; } = String.Empty;
			public double TotalDurationSeconds { get; set; } = 0;
			public bool HasSucceeded { get; set; } = false;
			public string Status { get; set; } = String.Empty;
			public string URLLink { get; set; } = String.Empty;
			public int BuildChangeList { get; set; } = 0;
			public TestRole? MainRole { get; set; }
			public List<TestRole>? Roles { get; set; } = new List<TestRole>();
			public string TestResult { get; set; } = "Invalid";
			public List<string> Logs { get; set; } = new List<string>();
			public List<string> Errors { get; set; } = new List<string>();
			public List<string> Warnings { get; set; } = new List<string>();
			public Dictionary<string, string> Metadata { get; set; } = new Dictionary<string, string>();
		}

		async Task AddSimpleReportData(IJob job, IJobStep step, ObjectId testDataId, BsonDocument testDoc)
		{
			try
			{
				SimpleTestData testData = BsonSerializer.Deserialize<SimpleTestData>(testDoc);

				TestMetaId? metaId = await AddTestMeta(testData.Metadata, job, step, testData.TestName);

				if (metaId == null)
				{
					throw new Exception($"Unable to add or update simple test report for {testData.TestName}, unable to generate meta data");
				}

				TestDocument? test = await AddOrUpdateTest(metaId.Value, testData.TestName);

				if (test == null)
				{
					throw new Exception($"Unable to add or update simple test report for {testData.TestName}, unable to generate test document");
				}

				TestDataRefDocument testRef = new TestDataRefDocument(job.StreamId);
				testRef.StreamId = job.StreamId;
				testRef.Metadata = metaId.Value;
				testRef.BuildChangeList = testData.BuildChangeList <= 0 ? job.Change : testData.BuildChangeList;
				testRef.Duration = TimeSpan.FromSeconds(testData.TotalDurationSeconds);
				testRef.TestId = test.Id;
				testRef.Outcome = testData.HasSucceeded ? TestOutcome.Success : TestOutcome.Unspecified;

				if (testRef.Outcome == TestOutcome.Unspecified)
				{
					switch (testData.TestResult)
					{
						case "Passed":
							testRef.Outcome = TestOutcome.Success;
							break;
						case "Failed":
						case "Cancelled":
						case "TimedOut":
							testRef.Outcome = TestOutcome.Failure;
							break;
					}
				}

				await AddTestRef(testRef);
				await _testDetails.InsertOneAsync(new TestDataDetailsDocument(testRef.Id, new List<ObjectId> { testDataId }));

			}
			catch (Exception ex)
			{
				_logger.LogWarning(ex, "Exception while adding test data simple report, jobId: {JobId} stepId: {StepId}", job.Id, step.Id);
			}
		}

		async Task AddTestSessionReportData(IJob job, IJobStep step, List<TestDataDocument> documents, List<AutomatedTestSessionData> sessions, List<UnrealAutomatedTestData> tests)
		{
			HashSet<string> suites = new HashSet<string>();

			for (int i = 0; i < sessions.Count; i++)
			{
				AutomatedTestSessionData session = sessions[i];

				AutomatedTestSessionData.TestSessionInfoType? info = session.TestSessionInfo;
				if (info == null)
				{
					return;
				}

				foreach (KeyValuePair<string, AutomatedTestSessionData.SessionTest> item in info.Tests)
				{
					AutomatedTestSessionData.SessionTest test = item.Value;

					if (!String.IsNullOrEmpty(test.Suite))
					{
						suites.Add(test.Suite);
					}
				}
			}

			if (suites.Count == 0)
			{
				return;
			}

			foreach (string suite in suites)
			{
				for (int i = 0; i < sessions.Count; i++)
				{
					HashSet<TestId> suiteTestIds = new HashSet<TestId>();
					Dictionary<string, TestDocument> suiteTestDocuments = new Dictionary<string, TestDocument>();

					AutomatedTestSessionData session = sessions[i];
					AutomatedTestSessionData.TestSessionInfoType? info = session.TestSessionInfo;
					if (info == null)
					{
						continue;
					}

					TestMetaId? metaId = await AddTestMeta(session.Metadata, job, step, "Automated Test Session");

					if (metaId == null)
					{
						throw new Exception($"Unable to add or update simple test report for automated test session, unable to generate meta data");
					}

					List<AutomatedTestSessionData.SessionTest> suiteTests = info.Tests.Where(x => x.Value.Suite == suite).Select(x => x.Value).ToList();

					foreach (AutomatedTestSessionData.SessionTest test in suiteTests)
					{

						for (int j = 0; j < tests.Count; j++)
						{
							UnrealAutomatedTestData testData = tests[j];

							// attempt to find the unreal test, which holds some optional data
							UnrealAutomatedTestData.UnrealTest? utest = null;
							for (int k = 0; k < testData.Tests.Count; k++)
							{
								UnrealAutomatedTestData.UnrealTest u = testData.Tests[k];
								if (test.Name == u.FullTestPath && test.DeviceAppInstanceName.Contains(u.DeviceInstance, StringComparer.OrdinalIgnoreCase))
								{
									utest = u;
									break;
								}
							}

							string? displayName = null;
							if (utest != null && !String.IsNullOrEmpty(utest.TestDisplayName))
							{
								displayName = utest.TestDisplayName;
							}

							TestDocument? testDoc = await AddOrUpdateTest(metaId!.Value, test.Name, suite, displayName);

							if (testDoc == null)
							{
								_logger.LogWarning("Skipping adding suite test {TestName} metaId: {MetaId} jobId: {JobId} stepId: {StepId}", test.Name, metaId!.Value, job.Id, step.Id);
								continue;
							}

							suiteTestDocuments.Add(test.Name, testDoc);
							suiteTestIds.Add(testDoc.Id);
						}
					}

					// register test suite
					if (suiteTestIds.Count > 0)
					{
						TestSuiteDocument? testSuite = await AddOrUpdateTestSuite(suite, metaId.Value, suiteTestIds.ToList());

						if (testSuite == null)
						{
							continue;
						}

						TestDataRefDocument testRef = new TestDataRefDocument(job.StreamId);
						testRef.StreamId = job.StreamId;
						testRef.Metadata = metaId.Value;
						testRef.BuildChangeList = job.Change;
						testRef.Duration = TimeSpan.FromSeconds(session.TestSessionInfo!.TimeElapseSec);
						testRef.Outcome = TestOutcome.Unspecified;
						testRef.SuiteId = testSuite.Id;

						int skipCount = 0;
						int warningCount = 0;
						int errorCount = 0;

						// populate the suite tests
						List<SuiteTestData> suiteTestData = new List<SuiteTestData>();
						foreach (AutomatedTestSessionData.SessionTest test in suiteTests)
						{
							if (test.WarningCount > 0)
							{
								warningCount++;
							}

							if (test.ErrorCount > 0)
							{
								errorCount++;
							}

							TestDocument document = suiteTestDocuments[test.Name];
							TestOutcome outcome = TestOutcome.Unspecified;

							switch (test.State)
							{
								case "Success":
									outcome = TestOutcome.Success;
									break;
								case "Skipped":
									outcome = TestOutcome.Skipped;
									skipCount++;
									break;
								case "Fail":
									outcome = TestOutcome.Failure;
									break;
							}

							suiteTestData.Add(new SuiteTestData(document.Id, outcome, TimeSpan.FromSeconds(test.TimeElapseSec), test.TestUID));
						}

						testRef.SuiteSkipCount = skipCount;
						testRef.SuiteErrorCount = errorCount;
						testRef.SuiteWarningCount =warningCount;

						// and add to collections
						await AddTestRef(testRef);
						await _testDetails.InsertOneAsync(new TestDataDetailsDocument(testRef.Id, documents.Select(x => x.Id).ToList(), suiteTestData));
					}
					else
					{
						_logger.LogWarning("Skipping suite {SuiteName} with not test ids, jobId: {JobId} stepId: {StepId}", suite, job.Id, step.Id);
					}
				}
			}
		}
	}
}
