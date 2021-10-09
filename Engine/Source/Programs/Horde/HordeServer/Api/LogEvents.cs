// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Information about an uploaded event
	/// </summary>
	public class GetLogEventResponse
	{
		/// <summary>
		/// Unique id of the log containing this event
		/// </summary>
		public string LogId { get; set; }

		/// <summary>
		/// Severity of this event
		/// </summary>
		public EventSeverity Severity { get; set; }

		/// <summary>
		/// Index of the first line for this event
		/// </summary>
		public int LineIndex { get; set; }

		/// <summary>
		/// Number of lines in the event
		/// </summary>
		public int LineCount { get; set; }

		/// <summary>
		/// The issue id associated with this event
		/// </summary>
		public int? IssueId { get; set; }

		/// <summary>
		/// The structured message data for this event
		/// </summary>
		public List<JsonElement> Lines { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Event">The event to construct from</param>
		/// <param name="EventData">The event data</param>
		/// <param name="IssueId">The issue for this event</param>
		public GetLogEventResponse(ILogEvent Event, ILogEventData EventData, int? IssueId)
		{
			this.Severity = Event.Severity;
			this.LogId = Event.LogId.ToString();
			this.LineIndex = Event.LineIndex;
			this.LineCount = Event.LineCount;
			this.IssueId = IssueId;
			this.Lines = EventData.Lines.ConvertAll(x => x.Data);
		}
	}
}
