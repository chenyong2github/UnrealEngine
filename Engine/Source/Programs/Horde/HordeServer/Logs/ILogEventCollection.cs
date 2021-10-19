// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using Serilog.Events;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Interface for a collection of event documents
	/// </summary>
	public interface ILogEventCollection
	{
		/// <summary>
		/// Creates a new event
		/// </summary>
		/// <param name="NewEvent">The new event to vreate</param>
		Task AddAsync(NewLogEventData NewEvent);

		/// <summary>
		/// Creates a new event
		/// </summary>
		/// <param name="NewEvents">List of events to create</param>
		Task AddManyAsync(List<NewLogEventData> NewEvents);

		/// <summary>
		/// Finds events within a log file
		/// </summary>
		/// <param name="LogId">Unique id of the log containing this event</param>
		/// <param name="Index">Start index within the matching results</param>
		/// <param name="Count">Maximum number of results to return</param>
		/// <returns>List of events matching the query</returns>
		Task<List<ILogEvent>> FindAsync(LogId LogId, int? Index = null, int? Count = null);

		/// <summary>
		/// Finds a list of events for a set of spans
		/// </summary>
		/// <param name="SpanIds">The span ids</param>
		/// <param name="LogIds">List of log ids to query</param>
		/// <param name="Index">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of events for this issue</returns>
		Task<List<ILogEvent>> FindEventsForSpansAsync(IEnumerable<ObjectId> SpanIds, LogId[]? LogIds = null, int Index = 0, int Count = 10);

		/// <summary>
		/// Delete all the events for a log file
		/// </summary>
		/// <param name="LogId">Unique id of the log</param>
		/// <returns>Async task</returns>
		Task DeleteLogAsync(LogId LogId);

		/// <summary>
		/// Update the span for an event
		/// </summary>
		/// <param name="Events">The events to update</param>
		/// <param name="SpanId">New span id</param>
		/// <returns>Async task</returns>
		Task AddSpanToEventsAsync(IEnumerable<ILogEvent> Events, ObjectId SpanId);
	}
}
