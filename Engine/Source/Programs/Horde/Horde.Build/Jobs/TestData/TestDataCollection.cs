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

	/// <summary>
	/// Tests which are run in a  stream
	/// </summary>
	public class TestStream
	{
		/// <summary>
		/// The associated stream tests are running in
		/// </summary>
		public StreamId StreamId { get; set; }

		/// <summary>
		/// The individual tests running in the stream
		/// </summary>
		public List<ITest> Tests { get; set; }

		/// <summary>
		/// The tests suites running in the stream
		/// </summary>
		public List<ITestSuite> TestSuites { get; set; }

		/// <summary>
		/// The test meta data (environments) running in the stream
		/// </summary>
		public List<ITestMeta> TestMeta { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="streamId"></param>
		/// <param name="tests"></param>
		/// <param name="testSuites"></param>
		/// <param name="testMeta"></param>
		public TestStream(StreamId streamId, List<ITest> tests, List<ITestSuite> testSuites, List<ITestMeta> testMeta)
		{
			StreamId = streamId;
			Tests = tests;
			TestSuites = testSuites;
			TestMeta = testMeta;
		}
	}

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

			[BsonRequired, BsonElement("sid")]
			public StreamId StreamId { get; set; }
			
			[BsonRequired, BsonElement("n")]
			public string Name { get; set; }

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

			public TestDocument(StreamId streamId, string name, List<TestMetaId> meta, string? displayName = null)
			{
				Id = TestId.GenerateNewId();
				StreamId = streamId;
				Name = name;
				Metadata = meta;
				DisplayName = displayName;
			}
		}

		class TestSuiteDocument : ITestSuite
		{
			[BsonRequired, BsonId]
			public TestSuiteId Id { get; set; }

			[BsonRequired, BsonElement("sid")]
			public StreamId StreamId { get; set; }

			[BsonRequired, BsonElement("n")]
			public string Name { get; set; }

			[BsonRequired, BsonElement("t")]
			public List<TestId> Tests { get; set; }
			IReadOnlyList<TestId> ITestSuite.Tests => Tests;

			private TestSuiteDocument()
			{
				Name = String.Empty;
				Tests = new List<TestId>();
			}

			public TestSuiteDocument(StreamId streamId, string name, List<TestId> tests)
			{
				Id = TestSuiteId.GenerateNewId();
				StreamId = streamId;
				Name = name;
				Tests = tests;
			}
		}

		class SuiteTestData : ISuiteTestData
		{
			[BsonRequired, BsonElement("tid")]
			public TestId Id { get; set; }

			[BsonRequired, BsonElement("o")]
			public TestOutcome Outcome { get; set; }

			[BsonRequired, BsonElement("d")]
			public TimeSpan Duration { get; set; }

			[BsonRequired, BsonElement("uid")]
			public string UID { get; set; }

			public SuiteTestData(TestId id, TestOutcome outcome, TimeSpan duration, string uid)
			{
				Id = id;
				Outcome = outcome;
				Duration = duration;
				UID = uid;
			}
		}

		class TestDataRefDocument : ITestDataRef
		{
			[BsonRequired, BsonId]
			public ObjectId Id { get; set; }

			[BsonRequired,BsonElement("sid")]
			public StreamId StreamId { get; set; }

			[BsonRequired, BsonElement("m")]
			public TestMetaId Metadata { get; set; }

			[BsonRequired, BsonElement("bcl")]
			public int BuildChangeList { get; set; }

			[BsonRequired, BsonElement("tdid")]
			public ObjectId TestDataId { get; set; }

			[BsonRequired, BsonElement("d")]
			public TimeSpan Duration { get; set; }

			[BsonIgnoreIfNull, BsonElement("tid")]
			public TestId? TestId { get; set; }

			[BsonIgnoreIfNull, BsonElement("o")]
			public TestOutcome? Outcome { get; set; }

			[BsonIgnoreIfNull, BsonElement("suid")]
			public TestSuiteId? SuiteId { get; set; }

			[BsonIgnoreIfNull, BsonElement("sts")]
			public List<SuiteTestData>? SuiteTests { get; set; }

			IReadOnlyList<ISuiteTestData>? ITestDataRef.SuiteTests => SuiteTests;

			private TestDataRefDocument()
			{

			}

			public TestDataRefDocument(StreamId streamId)
			{
				Id = ObjectId.GenerateNewId();
				StreamId = streamId;
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
		/// Test platform collection
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
		/// Test suite collection
		/// </summary>
		readonly IMongoCollection<TestDataRefDocument> _testRefs;

		readonly ILogger _logger;

		readonly IOptionsMonitor<ServerSettings> _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="mongoService"></param>
		/// <param name="logger"></param>
		/// <param name="settings"></param>
#pragma warning disable CS8618 // Disabled for feature flag: Non-nullable field must contain a non-null value when exiting constructor. Consider declaring as nullable.
		public TestDataCollection(MongoService mongoService, ILogger<TestDataCollection> logger, IOptionsMonitor<ServerSettings> settings)
#pragma warning restore CS8618 
		{

			_logger = logger;
			_settings = settings;

			List<MongoIndex<TestDataDocument>> indexes = new List<MongoIndex<TestDataDocument>>();
			indexes.Add(keys => keys.Ascending(x => x.StreamId).Ascending(x => x.Change).Ascending(x => x.Key));
			indexes.Add(keys => keys.Ascending(x => x.JobId).Ascending(x => x.StepId).Ascending(x => x.Key), unique: true);
			_testDataDocuments = mongoService.GetCollection<TestDataDocument>("TestData", indexes);

			if (_settings.CurrentValue.FeatureFlags.EnableTestDataV2)
			{
				_logger.LogInformation("EnableTestDataV2 is true, creating new test data collections");

				List<MongoIndex<TestMetaDocument>> metaIndexes = new List<MongoIndex<TestMetaDocument>>();
				metaIndexes.Add(keys => keys.Ascending(x => x.ProjectName).Ascending(x => x.Platforms).Ascending(x => x.Configurations));
				_testMeta = mongoService.GetCollection<TestMetaDocument>("TestData.Meta", metaIndexes);

				List<MongoIndex<TestDocument>> testIndexes = new List<MongoIndex<TestDocument>>();
				testIndexes.Add(keys => keys.Ascending(x => x.StreamId));
				_tests = mongoService.GetCollection<TestDocument>("TestData.Tests", testIndexes);

				List<MongoIndex<TestSuiteDocument>> testSuiteIndexes = new List<MongoIndex<TestSuiteDocument>>();
				testSuiteIndexes.Add(keys => keys.Ascending(x => x.StreamId));
				_testSuites = mongoService.GetCollection<TestSuiteDocument>("TestData.TestSuites", testSuiteIndexes);

				List<MongoIndex<TestDataRefDocument>> testRefIndexes = new List<MongoIndex<TestDataRefDocument>>();
				testRefIndexes.Add(keys => keys.Ascending(x => x.StreamId).Ascending(x => x.Metadata).Descending(x => x.BuildChangeList).Ascending(x => x.TestId).Ascending(x => x.SuiteId));
				_testRefs = mongoService.GetCollection<TestDataRefDocument>("TestData.TestRefs", testRefIndexes);
			}
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
				ObjectId minTime = ObjectId.GenerateNewId(minCreateTime.Value);
				filter &= filterBuilder.Gte(x => x.Id!, minTime);

			}
			if (maxCreateTime != null)
			{
				ObjectId maxTime = ObjectId.GenerateNewId(maxCreateTime.Value);
				filter &= filterBuilder.Lte(x => x.Id!, maxTime);
			}

			if (metaIds != null && metaIds.Length > 0)
			{
				filter &= filterBuilder.In(x => x.Metadata, metaIds);
			}

			if (testIds != null && testIds.Length > 0)
			{
				filter &= filterBuilder.Ne(x => x.TestId, null);
				filter &= filterBuilder.In(x => x.TestId!.Value, testIds);
			}

			if (suiteIds != null && suiteIds.Length > 0)
			{
				filter &= filterBuilder.Ne(x => x.SuiteId, null);
				filter &= filterBuilder.In(x => x.SuiteId!.Value, suiteIds);
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
		public async Task<ITestData> AddAsync(IJob job, IJobStep step, string key, BsonDocument value)
		{
			// detailed test data
			TestDataDocument newTestData = new TestDataDocument(job, step, key, value);
			await _testDataDocuments.InsertOneAsync(newTestData);

			try
			{
				if (_settings.CurrentValue.FeatureFlags.EnableTestDataV2)
				{
					_logger.LogInformation("Attempting to add test report data");
					await AddTestReportData(job, step, value, newTestData.Id);
				}									
				else
				{
					_logger.LogInformation("Test report data skipped due to disabled feature flag");
				}
			}
			catch (Exception ex)
			{
				_logger.LogWarning(ex, "Exception while adding test data  report, jobId: {JobId} stepId: {StepId}", job.Id, step.Id);
			}

			return newTestData;
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

		private async Task<List<TestMetaDocument>> FindTestMeta(TestMetaId[] metaIds)
		{
			FilterDefinitionBuilder<TestMetaDocument> filterBuilder = Builders<TestMetaDocument>.Filter;
			FilterDefinition<TestMetaDocument> filter = FilterDefinition<TestMetaDocument>.Empty;
			filter &= filterBuilder.In(x => x.Id, metaIds);

			return await _testMeta.Find(filter).ToListAsync();
		}

		private async Task<List<TestDocument>> FindTests(StreamId[] streamIds)
		{
			FilterDefinitionBuilder<TestDocument> filterBuilder = Builders<TestDocument>.Filter;
			FilterDefinition<TestDocument> filter = FilterDefinition<TestDocument>.Empty;
			filter &= filterBuilder.In(x => x.StreamId, streamIds);

			return await _tests.Find(filter).ToListAsync();
		}

		private async Task<List<TestSuiteDocument>> FindTestSuites(StreamId[] streamIds)
		{
			FilterDefinitionBuilder<TestSuiteDocument> filterBuilder = Builders<TestSuiteDocument>.Filter;
			FilterDefinition<TestSuiteDocument> filter = FilterDefinition<TestSuiteDocument>.Empty;
			filter &= filterBuilder.In(x => x.StreamId, streamIds);

			return await _testSuites.Find(filter).ToListAsync();
		}

		/// <inheritdoc/>
		public async Task<List<TestStream>> FindTestStreams(StreamId[] streamIds)
		{
			List<TestDocument> tests = await FindTests(streamIds);
			List<TestSuiteDocument> suites = await FindTestSuites(streamIds);

			List<TestMetaId> metaIds = tests.SelectMany(t => t.Metadata).Distinct().ToList();
			List<TestMetaDocument> metaData = await FindTestMeta(metaIds.ToArray());
			
			List<TestStream> testStreams = new List<TestStream>();
			for (int i = 0; i < streamIds.Length; i++)
			{
				StreamId id = streamIds[i];
				List<ITest> streamTests = tests.Where(x => x.StreamId == id).ToList<ITest>();
				List<ITestSuite> streamSuites = suites.Where(x => x.StreamId == id).ToList<ITestSuite>();
				List<TestMetaId> streamMetaIds = streamTests.SelectMany(t => t.Metadata).Distinct().ToList();
				List<ITestMeta> streamMeta = metaData.Where(x => streamMetaIds.Contains(x.Id)).ToList<ITestMeta>();
				testStreams.Add(new TestStream(id, streamTests, streamSuites, streamMeta));
			}

			return testStreams;
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

		async Task<TestDocument?> AddOrUpdateTest(IJob job, TestMetaId metaId, string testName, string? displayName = null)
		{			
			// register the test
			TestDocument? test = await _tests.Find(x => x.StreamId == job.StreamId && x.Name == testName).FirstOrDefaultAsync();
			if (test == null)
			{
				test = new TestDocument(job.StreamId, testName, new List<TestMetaId>() { metaId }, displayName);
				await _tests.InsertOneAsync(test);
			}
			else
			{
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

		private async Task<TestSuiteDocument?> AddOrUpdateTestSuite(IJob job, string suiteName, List<TestId> testIds)
		{
			TestSuiteDocument? suite = await _testSuites.Find(x => x.StreamId == job.StreamId && x.Name == suiteName).FirstOrDefaultAsync();

			if (suite == null)
			{
				suite = new TestSuiteDocument(job.StreamId, suiteName, testIds);
				await _testSuites.InsertOneAsync(suite);
			}
			else
			{
				List<TestId> newIds = suite.Tests.Union(testIds).ToList();

				if (newIds.Count != suite.Tests.Count)
				{
					suite.Tests = newIds;

					FilterDefinitionBuilder<TestSuiteDocument> ebuilder = Builders<TestSuiteDocument>.Filter;
					FilterDefinition<TestSuiteDocument> efilter = ebuilder.Eq(x => x.Id, suite.Id);

					UpdateDefinitionBuilder<TestSuiteDocument> updateBuilder = Builders<TestSuiteDocument>.Update;
					UpdateDefinition<TestSuiteDocument> update = updateBuilder.Set(x => x.Tests, suite.Tests);
					await _testSuites.UpdateOneAsync(efilter, update);
				}
			}

			return suite;
		}

		async Task AddTestReportData(IJob job, IJobStep step, BsonDocument rootDoc, ObjectId testDataId)
		{
			List<AutomatedTestSessionData> sessions = new List<AutomatedTestSessionData>();
			List<UnrealAutomatedTestData> tests = new List<UnrealAutomatedTestData>();

			BsonArray items = rootDoc.GetValue("Items").AsBsonArray;
			foreach (BsonValue item in items.ToList())
			{
				if (!item.IsBsonDocument)
				{
					continue;
				}

				BsonDocument testData = item.AsBsonDocument;

				int version = testData.GetValue("Version", new BsonInt32(0)).AsInt32;
				if (version < 1)
				{
					_logger.LogWarning("Test data does not have version and needs to be updated in stream for job {JobId} step {StepId}", job.Id, step.Id);
					return;
				}

				BsonValue? value;
				if (!testData.TryGetPropertyValue("Data.Type", BsonType.String, out value))
				{
					continue;
				}

				string type = value.AsString;
				BsonDocument data = testData.GetValue("Data").AsBsonDocument;

				if (type == "Simple Report")
				{
					await AddSimpleReportData(job, step, testDataId, data);
					continue;
				}

				if (type == "Automated Test Session")
				{
					sessions.Add(BsonSerializer.Deserialize<AutomatedTestSessionData>(data));
				}

				if (type == "Unreal Automated Tests")
				{
					tests.Add(BsonSerializer.Deserialize<UnrealAutomatedTestData>(data));
				}
			}

			if (sessions.Count > 0)
			{
				await AddTestSessionReportData(job, step, testDataId, sessions, tests);
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

				TestDocument? test = await AddOrUpdateTest(job, metaId.Value, testData.TestName);

				if (test == null)
				{
					throw new Exception($"Unable to add or update simple test report for {testData.TestName}, unable to generate test document");
				}

				TestDataRefDocument testRef = new TestDataRefDocument(job.StreamId);
				testRef.StreamId = test.StreamId;
				testRef.Metadata = metaId.Value;
				testRef.BuildChangeList = testData.BuildChangeList;
				testRef.Duration = TimeSpan.FromSeconds(testData.TotalDurationSeconds);
				testRef.TestId = test.Id;
				testRef.TestDataId = testDataId;
				testRef.Outcome = TestOutcome.Unspecified;

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

				await _testRefs.InsertOneAsync(testRef);

			}
			catch (Exception ex)
			{
				_logger.LogWarning(ex, "Exception while adding test data simple report, jobId: {JobId} stepId: {StepId}", job.Id, step.Id);
			}
		}

		async Task AddTestSessionReportData(IJob job, IJobStep step, ObjectId testDataId, List<AutomatedTestSessionData> sessions, List<UnrealAutomatedTestData> tests)
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

							TestDocument? testDoc = await AddOrUpdateTest(job, metaId!.Value, test.Name, displayName);

							if (testDoc == null)
							{
								_logger.LogWarning("Skipping adding suite test {TestName} metaId: {MetaId} testId: {TestId} jobId: {JobId} stepId: {StepId}", test.Name, metaId?.Value, testDoc?.Id, job.Id, step.Id);
								continue;
							}

							suiteTestDocuments.Add(test.Name, testDoc);
							suiteTestIds.Add(testDoc.Id);
						}
					}

					// register test suite
					if (suiteTestIds.Count > 0)
					{
						TestSuiteDocument? testSuite = await AddOrUpdateTestSuite(job, suite, suiteTestIds.ToList());

						if (testSuite == null)
						{
							continue;
						}

						TestDataRefDocument testRef = new TestDataRefDocument(job.StreamId);
						testRef.StreamId = job.StreamId;
						testRef.Metadata = metaId.Value;
						testRef.BuildChangeList = job.Change;
						testRef.Duration = TimeSpan.FromSeconds(session.TestSessionInfo!.TimeElapseSec);
						testRef.TestDataId = testDataId;
						testRef.Outcome = TestOutcome.Unspecified;
						testRef.SuiteId = testSuite.Id;
						testRef.SuiteTests = new List<SuiteTestData>();

						// populate the suite tests

						foreach (AutomatedTestSessionData.SessionTest test in suiteTests)
						{

							TestDocument document = suiteTestDocuments[test.Name];
							TestOutcome outcome = TestOutcome.Unspecified;

							switch (test.State)
							{
								case "Success":
									outcome = TestOutcome.Success;
									break;
								case "Skipped":
									outcome = TestOutcome.Skipped;
									break;
								case "Fail":
									outcome = TestOutcome.Failure;
									break;
							}

							testRef.SuiteTests.Add(new SuiteTestData(document.Id, outcome, TimeSpan.FromSeconds(test.TimeElapseSec), test.TestUID));
						}

						await _testRefs.InsertOneAsync(testRef);
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
