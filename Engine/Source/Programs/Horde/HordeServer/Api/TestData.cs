// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using MongoDB.Bson.Serialization;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Response object describing test data to store
	/// </summary>
	public class CreateTestDataRequest
	{
		/// <summary>
		/// The job which produced the data
		/// </summary>
		[Required]
		public string JobId { get; set; } = String.Empty;

		/// <summary>
		/// The step that ran
		/// </summary>
		[Required]
		public string StepId { get; set; } = String.Empty;

		/// <summary>
		/// Key used to identify the particular data
		/// </summary>
		public string Key { get; set; } = String.Empty;

		/// <summary>
		/// The data stored for this test
		/// </summary>
		[Required]
		public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();
	}

	/// <summary>
	/// Response object describing the created document
	/// </summary>
	public class CreateTestDataResponse
	{
		/// <summary>
		/// The id for the new document
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Id of the new document</param>
		public CreateTestDataResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Response object describing test results
	/// </summary>
	public class GetTestDataResponse
	{
		/// <summary>
		/// Unique id of the test data
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Stream that generated the test data
		/// </summary>
		public string StreamId { get; set; }

		/// <summary>
		/// The template reference id
		/// </summary>
		public string TemplateRefId { get; set; }

		/// <summary>
		/// The job which produced the data
		/// </summary>
		public string JobId { get; set; }

		/// <summary>
		/// The step that ran
		/// </summary>
		public string StepId { get; set; }

		/// <summary>
		/// The changelist number that contained the data
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Key used to identify the particular data
		/// </summary>
		public string Key { get; set; }

		/// <summary>
		/// The data stored for this test
		/// </summary>
		public Dictionary<string, object> Data { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TestData">Test data to construct from</param>
		internal GetTestDataResponse(ITestData TestData)
		{
			this.Id = TestData.Id.ToString();
			this.StreamId = TestData.StreamId.ToString();
			this.TemplateRefId = TestData.TemplateRefId.ToString();
			this.JobId = TestData.JobId.ToString();
			this.StepId = TestData.StepId.ToString();
			this.Change = TestData.Change;
			this.Key = TestData.Key;
			this.Data = BsonSerializer.Deserialize<Dictionary<string, object>>(TestData.Data);
		}
	}
}
