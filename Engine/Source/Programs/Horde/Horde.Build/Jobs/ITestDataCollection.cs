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

namespace HordeServer.Collections
{
	using JobId = ObjectId<IJob>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Collection of test data documents
	/// </summary>
	public interface ITestDataCollection
	{
		/// <summary>
		/// Creates a new test data document
		/// </summary>
		/// <param name="Job">The job containing the step</param>
		/// <param name="Step">The step producing the data</param>
		/// <param name="Key">Key identifying the test</param>
		/// <param name="Value">The data to store</param>
		/// <returns>The new stream document</returns>
		Task<ITestData> AddAsync(IJob Job, IJobStep Step, string Key, BsonDocument Value);

		/// <summary>
		/// Gets a stream by ID
		/// </summary>
		/// <param name="Id">Unique id of the stream</param>
		/// <returns>The stream document</returns>
		Task<ITestData?> GetAsync(ObjectId Id);

		/// <summary>
		/// Searches for test data that matches a set of criteria
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="MinChange">The minimum changelist number to return (inclusive)</param>
		/// <param name="MaxChange">The maximum changelist number to return (inclusive)</param>
		/// <param name="JobId">The job id</param>
		/// <param name="StepId">The unique step id</param>
		/// <param name="Key">Key identifying the result to return</param>
		/// <param name="Index">Offset within the results to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>The stream document</returns>
		Task<List<ITestData>> FindAsync(StreamId? StreamId, int? MinChange, int? MaxChange, JobId? JobId, SubResourceId? StepId, string? Key = null, int Index = 0, int Count = 10);

		/// <summary>
		/// Delete the test data
		/// </summary>
		/// <param name="Id">Unique id of the test data</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(ObjectId Id);
	}
}
