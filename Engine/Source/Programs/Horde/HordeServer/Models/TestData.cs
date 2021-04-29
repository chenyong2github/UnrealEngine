// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServer.Models
{
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
		ObjectId JobId { get; }

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
