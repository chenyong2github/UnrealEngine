// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser
{
	enum LogEventPriority
	{
		Lowest,
		Low,
		BelowNormal,
		Normal,
		AboveNormal,
		High,
		Highest,
	}

	/// <summary>
	/// Stores information about a parsed log event
	/// </summary>
	class LogEvent
	{
		/// <summary>
		/// The priority of this matched event
		/// </summary>
		public LogEventPriority Priority
		{
			get;
		}

		/// <summary>
		/// The log level
		/// </summary>
		public LogLevel Level
		{
			get; set;
		}

		/// <summary>
		/// Unique id associated with this event. See <see cref="HordeCommon.KnownLogEvents"/> for possible values.
		/// </summary>
		public EventId EventId
		{
			get;
		}

		/// <summary>
		/// The minimum line number from the parsed output corresponding to this event
		/// </summary>
		public int MinLineNumber
		{
			get; set;
		}

		/// <summary>
		/// The maximum line number from the parsed output corresponding to this event
		/// </summary>
		public int MaxLineNumber
		{
			get; set;
		}

		/// <summary>
		/// State for the log event
		/// </summary>
		public object State
		{
			get;
		}

		/// <summary>
		/// List of child events. If set, these sub-events will be posted to the server instead of the containing event.
		/// </summary>
		public List<LogEvent>? ChildEvents
		{
			get; set;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Priority">Value indicating the confidence/specificity of this match</param>
		/// <param name="Level">Severity of this event</param>
		/// <param name="EventId">Id for this event type</param>
		/// <param name="MinLineNumber">The minimum line number in the buffer</param>
		/// <param name="MaxLineNumber">The maximum line number in the buffer</param>
		/// <param name="State">The log state object</param>
		public LogEvent(LogEventPriority Priority, LogLevel Level, EventId EventId, int MinLineNumber, int MaxLineNumber, object State)
		{
			this.Priority = Priority;
			this.Level = Level;
			this.EventId = EventId;
			this.MinLineNumber = MinLineNumber;
			this.MaxLineNumber = MaxLineNumber;
			this.State = State;
		}

		/// <summary>
		/// Tries to get a parameter with a particular dotted property name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		/// <returns>True is the property was found</returns>
		public bool TryGetSpan(string Name, [NotNullWhen(true)] out LogEventSpan? Value)
		{
			IEnumerable<KeyValuePair<string, object>>? Dictionary = State as IEnumerable<KeyValuePair<string, object>>;
			if(Dictionary == null)
			{
				Value = null;
				return false;
			}

			KeyValuePair<string, object> Pair = Dictionary.FirstOrDefault(x => x.Key.Equals(Name, StringComparison.Ordinal));
			if(Pair.Value == null)
			{
				Value = null;
				return false;
			}

			Value = Pair.Value as LogEventSpan;
			return Value != null;
		}

		/// <summary>
		/// Renders this event
		/// </summary>
		/// <returns>The event text</returns>
		public override string ToString()
		{
			return State.ToString() ?? String.Empty;
		}
	}
}
