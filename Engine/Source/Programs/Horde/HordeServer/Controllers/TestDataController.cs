// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Server.Kestrel.Core.Features;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;

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
		private JobService JobService;

		/// <summary>
		/// Collection of test data documents
		/// </summary>
		private readonly ITestDataCollection TestDataCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="JobService">The job service singleton</param>
		/// <param name="TestDataCollection">Collection of test data documents</param>
		public TestDataController(JobService JobService, ITestDataCollection TestDataCollection)
		{
			this.JobService = JobService;
			this.TestDataCollection = TestDataCollection;
		}

		/// <summary>
		/// Creates a new TestData document
		/// </summary>
		/// <returns>The stream document</returns>
		[HttpPost]
		[Route("/api/v1/testdata")]
		public async Task<ActionResult<CreateTestDataResponse>> CreateAsync(CreateTestDataRequest Request)
		{
			IJob? Job = await JobService.GetJobAsync(new JobId(Request.JobId));
			if (Job == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(Job, AclAction.UpdateJob, User, null))
			{
				return Forbid();
			}

			IJobStep? JobStep;
			if (!Job.TryGetStep(Request.StepId.ToSubResourceId(), out JobStep))
			{
				return NotFound();
			}

			ITestData TestData = await TestDataCollection.AddAsync(Job, JobStep, Request.Key, new BsonDocument(Request.Data));
			return new CreateTestDataResponse(TestData.Id.ToString());
		}

		/// <summary>
		/// Searches for test data that matches a set of criteria
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="MinChange">The minimum changelist number to return (inclusive)</param>
		/// <param name="MaxChange">The maximum changelist number to return (inclusive)</param>
		/// <param name="JobId">The job id</param>
		/// <param name="JobStepId">The unique step id</param>
		/// <param name="Key">Key identifying the result to return</param>
		/// <param name="Index">Offset within the results to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <param name="Filter">Filter for properties to return</param>
		/// <returns>The stream document</returns>
		[HttpGet]
		[Route("/api/v1/testdata")]
		[ProducesResponseType(typeof(List<GetTestDataResponse>), 200)]
		public async Task<ActionResult<List<object>>> FindTestDataAsync([FromQuery] string? StreamId = null, [FromQuery] int? MinChange = null, [FromQuery] int? MaxChange = null, string? JobId = null, string? JobStepId = null, string? Key = null, int Index = 0, int Count = 10, PropertyFilter? Filter = null)
		{
			StreamId? StreamIdValue = null;
			if(StreamId != null)
			{
				StreamIdValue = new StreamId(StreamId);
			}

			JobPermissionsCache Cache = new JobPermissionsCache();

			List<object> Results = new List<object>();

			List<ITestData> Documents = await TestDataCollection.FindAsync(StreamIdValue, MinChange, MaxChange, JobId?.ToObjectId<IJob>(), JobStepId?.ToSubResourceId(), Key, Index, Count);
			foreach (ITestData Document in Documents)
			{
				if (await JobService.AuthorizeAsync(Document.JobId, AclAction.ViewJob, User, Cache))
				{
					Results.Add(PropertyFilter.Apply(new GetTestDataResponse(Document), Filter));
				}
			}

			return Results;
		}

		/// <summary>
		/// Retrieve information about a specific issue
		/// </summary>
		/// <param name="TestDataId">Id of the document to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>List of matching agents</returns>
		[HttpGet]
		[Route("/api/v1/testdata/{TestDataId}")]
		[ProducesResponseType(typeof(GetTestDataResponse), 200)]
		public async Task<ActionResult<object>> GetTestDataAsync(string TestDataId, [FromQuery] PropertyFilter? Filter = null)
		{
			ITestData? TestData = await TestDataCollection.GetAsync(TestDataId.ToObjectId());
			if (TestData == null)
			{
				return NotFound();
			}
			if (!await JobService.AuthorizeAsync(TestData.JobId, AclAction.ViewJob, User, null))
			{
				return Forbid();
			}

			return PropertyFilter.Apply(new GetTestDataResponse(TestData), Filter);
		}
	}
}
