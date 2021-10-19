// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Stores information about the results of a test
	/// </summary>
	public interface ITestData
	{
		/// <summary>
		/// Unique id of the test data
		/// </summary>
		ObjectId Id { get; }

		/// <summary>
		/// Stream that generated the test data
		/// </summary>
		StreamId StreamId { get; }

		/// <summary>
		/// The template reference id
		/// </summary>
		TemplateRefId TemplateRefId { get; }

		/// <summary>
		/// The job which produced the data
		/// </summary>
		JobId JobId { get; }

		/// <summary>
		/// The step that ran
		/// </summary>
		SubResourceId StepId { get; }

		/// <summary>
		/// The changelist number that contained the data
		/// </summary>
		int Change { get; }

		/// <summary>
		/// Key used to identify the particular data
		/// </summary>
		string Key { get; }

		/// <summary>
		/// The data stored for this test
		/// </summary>
		BsonDocument Data { get; }
	}
}
