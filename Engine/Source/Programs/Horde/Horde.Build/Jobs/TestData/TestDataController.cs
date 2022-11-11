// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Horde.Build.Acls;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using EpicGames.Core;

namespace Horde.Build.Jobs.TestData
{
	
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;
	using TestId = ObjectId<ITest>;
	using TestSuiteId = ObjectId<ITestSuite>;
	using TestMetaId = ObjectId<ITestMeta>;

	/// <summary>
	/// Controller for the /api/v1/testdata endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class TestDataController : ControllerBase
	{
		/// <summary>
		/// Collection of job documents
		/// </summary>
		private readonly JobService _jobService;

		/// <summary>
		/// Collection of test data documents
		/// </summary>
		private readonly ITestDataCollection _testDataCollection;

		readonly TestDataService _testDataService;

		readonly StreamService _streamService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="testDataService"></param>
		/// <param name="streamService"></param>
		/// <param name="jobService"></param>
		/// <param name="testDataCollection"></param>
		public TestDataController(TestDataService testDataService, StreamService streamService, JobService jobService, ITestDataCollection testDataCollection)
		{
			_jobService = jobService;
			_testDataCollection = testDataCollection;
			_testDataService = testDataService;
			_streamService = streamService;
		}

		/// <summary>
		/// Get metadata 
		/// </summary>
		/// <param name="projects"></param>
		/// <param name="platforms"></param>
		/// <param name="targets"></param>
		/// <param name="configurations"></param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v2/testdata/metadata")]
		[ProducesResponseType(typeof(List<GetTestMetaResponse>), 200)]
		public async Task<ActionResult<List<GetTestMetaResponse>>> GetTestMetaAsync(
			[FromQuery(Name = "projects")] string[]? projects = null,
			[FromQuery(Name = "platform")] string[]? platforms = null,
			[FromQuery(Name = "target")] string[]? targets = null,
			[FromQuery(Name = "configuration")] string[]? configurations = null)
		{
			List<GetTestMetaResponse> responses = new List<GetTestMetaResponse>();
			List<ITestMeta> metaData = await _testDataService.FindTestMeta(projects, platforms, configurations, targets);
			metaData.ForEach(m => responses.Add(new GetTestMetaResponse(m)));
			return responses;
		}

		/// <summary>
		/// Get stream test data for the provided ids
		/// </summary>
		/// <param name="streamIds"></param>
		/// <param name="metaIds"></param>
		/// <param name="minCreateTime"></param>
		/// <param name="maxCreateTime"></param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v2/testdata/streams")]
		[ProducesResponseType(typeof(List<GetTestStreamResponse>), 200)]
		public async Task<ActionResult<List<GetTestStreamResponse>>> GetTestStreamsAsync(
			[FromQuery(Name = "Id")] string[] streamIds,
			[FromQuery(Name = "Mid")] string[] metaIds,
			[FromQuery] DateTimeOffset minCreateTime,
			[FromQuery] DateTimeOffset? maxCreateTime = null)
		{
			StreamId[] streamIdValues = Array.ConvertAll(streamIds, x => new StreamId(x));

			List<StreamId> queryStreams = new List<StreamId>();
			List<GetTestStreamResponse> responses = new List<GetTestStreamResponse>();

			// authorize streams
			foreach (StreamId streamId in streamIdValues)
			{
				IStream? stream = await _streamService.GetCachedStream(streamId);
				if (stream != null)
				{
					if (await _streamService.AuthorizeAsync(stream, AclAction.ViewJob, User, null))
					{
						queryStreams.Add(stream.Id);
					}
				}
			}

			if (queryStreams.Count == 0)
			{
				return responses;
			}

			List<TestStream> streams = await _testDataService.FindTestStreams(queryStreams.ToArray(), metaIds.ConvertAll(x => TestMetaId.Parse(x)).ToArray(), minCreateTime.UtcDateTime, maxCreateTime?.UtcDateTime);
			streams.ForEach(s => responses.Add(new GetTestStreamResponse(s.StreamId, s.Tests, s.TestSuites, s.TestMeta)));
			return responses;
		}

		/// <summary>
		/// Gets test data refs 
		/// </summary>
		/// <param name="streamIds"></param>
		/// <param name="testIds"></param>
		/// <param name="suiteIds"></param>
		/// <param name="metaIds"></param>
		/// <param name="minCreateTime"></param>
		/// <param name="maxCreateTime"></param>
		/// <param name="minChange"></param>
		/// <param name="maxChange"></param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v2/testdata/refs")]
		[ProducesResponseType(typeof(List<GetTestDataRefResponse>), 200)]
		public async Task<ActionResult<List<GetTestDataRefResponse>>> GetTestDataRefAsync(
			[FromQuery(Name = "Id")] string[] streamIds,
			[FromQuery(Name = "Mid")] string[] metaIds,
			[FromQuery(Name = "Tid")] string[]? testIds = null,
			[FromQuery(Name = "Sid")] string[]? suiteIds = null,			
			[FromQuery] DateTimeOffset? minCreateTime = null,
			[FromQuery] DateTimeOffset? maxCreateTime = null,
			[FromQuery] int? minChange = null, 
			[FromQuery] int? maxChange = null)
		{
			StreamId[] streamIdValues = Array.ConvertAll(streamIds, x => new StreamId(x));

			List<StreamId> queryStreams = new List<StreamId>();
			List<GetTestDataRefResponse> responses = new List<GetTestDataRefResponse>();

			// authorize streams
			foreach (StreamId streamId in streamIdValues)
			{
				IStream? stream = await _streamService.GetCachedStream(streamId);
				if (stream != null)
				{
					if (await _streamService.AuthorizeAsync(stream, AclAction.ViewJob, User, null))
					{
						queryStreams.Add(stream.Id);
					}
				}
			}

			if (queryStreams.Count == 0)
			{
				return responses;
			}

			List<ITestDataRef> dataRefs = await _testDataService.FindTestRefs(queryStreams.ToArray(), metaIds.ConvertAll(x => TestMetaId.Parse(x)).ToArray(), testIds, suiteIds, minCreateTime?.UtcDateTime, maxCreateTime?.UtcDateTime, minChange, maxChange);

			dataRefs.ForEach(d => responses.Add(new GetTestDataRefResponse(d)));

			return responses;
		}

		/// <summary>
		/// Creates a new TestData document
		/// </summary>
		/// <returns>The stream document</returns>
		[HttpPost]
		[Route("/api/v1/testdata")]
		public async Task<ActionResult<CreateTestDataResponse>> CreateAsync(CreateTestDataRequest request)
		{
			IJob? job = await _jobService.GetJobAsync(new JobId(request.JobId));
			if (job == null)
			{
				return NotFound();
			}
			if (!await _jobService.AuthorizeAsync(job, AclAction.UpdateJob, User, null))
			{
				return Forbid();
			}

			IJobStep? jobStep;
			if (!job.TryGetStep(request.StepId.ToSubResourceId(), out jobStep))
			{
				return NotFound();
			}
			
			List<ITestData> testData = await _testDataCollection.AddAsync(job, jobStep, new (string key, BsonDocument value)[] { (request.Key, new BsonDocument(request.Data))});
			return new CreateTestDataResponse(testData[0].Id.ToString());
		}

		/// <summary>
		/// Searches for test data that matches a set of criteria
		/// </summary>
		/// <param name="streamId">The stream id</param>
		/// <param name="minChange">The minimum changelist number to return (inclusive)</param>
		/// <param name="maxChange">The maximum changelist number to return (inclusive)</param>
		/// <param name="jobId">The job id</param>
		/// <param name="jobStepId">The unique step id</param>
		/// <param name="key">Key identifying the result to return</param>
		/// <param name="index">Offset within the results to return</param>
		/// <param name="count">Number of results to return</param>
		/// <param name="filter">Filter for properties to return</param>
		/// <returns>The stream document</returns>
		[HttpGet]
		[Route("/api/v1/testdata")]
		[ProducesResponseType(typeof(List<GetTestDataResponse>), 200)]
		public async Task<ActionResult<List<object>>> FindTestDataAsync([FromQuery] string? streamId = null, [FromQuery] int? minChange = null, [FromQuery] int? maxChange = null, string? jobId = null, string? jobStepId = null, string? key = null, int index = 0, int count = 10, PropertyFilter? filter = null)
		{
			StreamId? streamIdValue = null;
			if(streamId != null)
			{
				streamIdValue = new StreamId(streamId);
			}

			JobPermissionsCache cache = new JobPermissionsCache();

			List<object> results = new List<object>();

			List<ITestData> documents = await _testDataCollection.FindAsync(streamIdValue, minChange, maxChange, jobId?.ToObjectId<IJob>(), jobStepId?.ToSubResourceId(), key, index, count);
			foreach (ITestData document in documents)
			{
				if (await _jobService.AuthorizeAsync(document.JobId, AclAction.ViewJob, User, cache))
				{
					results.Add(PropertyFilter.Apply(new GetTestDataResponse(document), filter));
				}
			}

			return results;
		}

		/// <summary>
		/// Retrieve information about a specific issue
		/// </summary>
		/// <param name="testDataId">Id of the document to get information about</param>
		/// <param name="filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/testdata/{testDataId}")]
		[ProducesResponseType(typeof(GetTestDataResponse), 200)]
		public async Task<ActionResult<object>> GetTestDataAsync(string testDataId, [FromQuery] PropertyFilter? filter = null)
		{
			ITestData? testData = await _testDataCollection.GetAsync(testDataId.ToObjectId());
			if (testData == null)
			{
				return NotFound();
			}
			if (!await _jobService.AuthorizeAsync(testData.JobId, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			return PropertyFilter.Apply(new GetTestDataResponse(testData), filter);
		}
	}
}
