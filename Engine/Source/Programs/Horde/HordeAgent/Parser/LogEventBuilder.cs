// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser.Interfaces;
using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser
{
	/// <summary>
	/// Stores information about a span within a line
	/// </summary>
	class LogEventSpan
	{
		/// <summary>
		/// Starting offset within the line
		/// </summary>
		public int Offset
		{
			get;
		}

		/// <summary>
		/// Text for this span
		/// </summary>
		public string Text
		{
			get;
		}

		/// <summary>
		/// Storage for properties
		/// </summary>
		public Dictionary<string, object> Properties
		{
			get;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Offset">Starting offset within the line</param>
		/// <param name="Text">The text for this span</param>
		public LogEventSpan(int Offset, string Text)
		{
			this.Offset = Offset;
			this.Text = Text;
			this.Properties = new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
		}

		/// <summary>
		/// Converts this object to a string. This determines how the span will be rendered by the default console logger, so should return the original text.
		/// </summary>
		/// <returns>Original text for this span</returns>
		public override string ToString()
		{
			return Text;
		}
	}

	/// <summary>
	/// Individual line in the log output
	/// </summary>
	class LogEventLine
	{
		/// <summary>
		/// The raw text
		/// </summary>
		public string Text
		{
			get;
		}

		/// <summary>
		/// List of spans for markup
		/// </summary>
		public Dictionary<string, LogEventSpan> Spans
		{
			get;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">Text for the line</param>
		public LogEventLine(string Text)
		{
			this.Text = Text;
			this.Spans = new Dictionary<string, LogEventSpan>();
		}

		/// <summary>
		/// Adds a span containing markup on the source text
		/// </summary>
		/// <param name="Offset">Offset within the line</param>
		/// <param name="Length">Length of the span</param>
		/// <param name="Name">Name to use to identify the item in the format string</param>
		/// <returns>New span for the given range</returns>
		public LogEventSpan AddSpan(int Offset, int Length, string Name)
		{
			LogEventSpan Span = new LogEventSpan(Offset, Text.Substring(Offset, Length));
			Spans.Add(Name, Span);
			return Span;
		}

		/// <summary>
		/// Adds a span containing markup for a regex match group
		/// </summary>
		/// <param name="Group">The match group</param>
		/// <param name="Name">Name to use to identify the item in the format string</param>
		public LogEventSpan AddSpan(Group Group, string Name)
		{
			return AddSpan(Group.Index, Group.Length, Name);
		}

		/// <summary>
		/// Adds a span naming a regex match group, using the name of the group
		/// </summary>
		/// <param name="Group">The match group</param>
		public LogEventSpan AddSpan(Group Group)
		{
			return AddSpan(Group.Index, Group.Length, Group.Name);
		}

		/// <summary>
		/// Adds a span naming a regex match group, using the name of the group
		/// </summary>
		/// <param name="Group">The match group</param>
		/// <param name="Name">Name to use to identify the item in the format string</param>
		public LogEventSpan? TryAddSpan(Group Group, string Name)
		{
			if (Group.Success)
			{
				return AddSpan(Group, Name);
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Adds a span naming a regex match group, using the name of the group
		/// </summary>
		/// <param name="Group">The match group</param>
		public LogEventSpan? TryAddSpan(Group Group)
		{
			if (Group.Success)
			{
				return AddSpan(Group);
			}
			else
			{
				return null;
			}
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return Text;
		}
	}

	/// <summary>
	/// Allows building log events by annotating a window of lines around the current cursor position.
	/// </summary>
	class LogEventBuilder : ILogCursor
	{
		/// <summary>
		/// Formatted log event object
		/// </summary>
		class LogEventState : IEnumerable<KeyValuePair<string, object>>
		{
			/// <summary>
			/// Formatting string
			/// </summary>
			public string Format = String.Empty;

			/// <summary>
			/// Map of property name to value
			/// </summary>
			public Dictionary<string, object>? Properties;

			/// <summary>
			/// Adds a property to this object
			/// </summary>
			/// <param name="Name">Property name</param>
			/// <param name="Value">Property value</param>
			public void AddProperty(string Name, object Value)
			{
				if (Properties == null)
				{
					Properties = new Dictionary<string, object>();
				}
				Properties.Add(Name, Value);
			}

			/// <summary>
			/// Adds a property to this object
			/// </summary>
			/// <param name="Name">Property name</param>
			public bool HasProperty(string Name)
			{
				return Properties != null && Properties.ContainsKey(Name);
			}

			/// <summary>
			/// Enumerates all the properties in this object
			/// </summary>
			/// <returns>Property pairs</returns>
			public IEnumerator<KeyValuePair<string, object>> GetEnumerator()
			{
				if (Format != null)
				{
					yield return new KeyValuePair<string, object>(MessageTemplate.FormatPropertyName, Format);
				}

				if (Properties != null)
				{
					foreach (KeyValuePair<string, object> Pair in Properties)
					{
						yield return Pair;
					}
				}
			}

			/// <summary>
			/// Enumerates all the properties in this object
			/// </summary>
			/// <returns>Property pairs</returns>
			IEnumerator IEnumerable.GetEnumerator()
			{
				foreach (KeyValuePair<string, object> Pair in this)
				{
					yield return Pair;
				}
			}

			/// <summary>
			/// Formats this message as text
			/// </summary>
			/// <returns>Rendered text</returns>
			public override string ToString()
			{
				return MessageTemplate.Render(Format, Properties);
			}
		}

		/// <summary>
		/// Lines that are part of this collection
		/// </summary>
		List<LogEventLine> LineCollection = new List<LogEventLine>();

		/// <summary>
		/// Any additional arguments to include in the event
		/// </summary>
		Dictionary<string, object>? AdditionalProperties;

		/// <summary>
		/// The input log data
		/// </summary>
		public ILogCursor Cursor
		{
			get;
		}

		/// <inheritdoc/>
		public string? this[int Offset] => Cursor[Offset + Lines.Count];

		/// <inheritdoc/>
		public string? CurrentLine => this[0];

		/// <inheritdoc/>
		public int CurrentLineNumber => Cursor.CurrentLineNumber + Lines.Count;

		/// <summary>
		/// Retrieves the collection of lines currently stored by this builder
		/// </summary>
		public IReadOnlyList<LogEventLine> Lines
		{
			get { return LineCollection; }
		}

		/// <summary>
		/// The current number of lines
		/// </summary>
		public int LineCount
		{
			get
			{
				return LineCollection.Count;
			}
			set
			{
				int NewCount = value;
				while (NewCount > LineCollection.Count)
				{
					LineCollection.Add(new LogEventLine(Cursor[LineCollection.Count] ?? String.Empty));
				}
				if (NewCount < LineCollection.Count)
				{
					LineCollection.RemoveRange(NewCount, LineCollection.Count - NewCount);
				}
			}
		}
		/// <summary>
		/// The current maximum offset relative to the cursor position
		/// </summary>
		public int MaxOffset
		{
			get { return LineCount - 1; }
			set { LineCount = value + 1; }
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Cursor">Input log data</param>
		/// <param name="NumLines">Number of lines to add</param>
		public LogEventBuilder(ILogCursor Cursor, int NumLines = 1)
		{
			this.Cursor = Cursor;

			for (int Idx = 0; Idx < NumLines; Idx++)
			{
				LineCollection.Add(new LogEventLine(Cursor[Idx] ?? String.Empty));
			}
		}

		/// <summary>
		/// Adds the next line to the event
		/// </summary>
		/// <returns>The new line object</returns>
		public LogEventLine AddLine()
		{
			return Lines[++MaxOffset];
		}

		/// <summary>
		/// Adds an additional named property
		/// </summary>
		/// <param name="Name">Name of the argument</param>
		/// <param name="Value">Value to associate with it</param>
		public void AddProperty(string Name, object Value)
		{
			if(AdditionalProperties == null)
			{
				AdditionalProperties = new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
			}
			AdditionalProperties[Name] = Value;
		}

		/// <summary>
		/// Constructs a log event from the data in this builder
		/// </summary>
		/// <param name="Priority">Priority for the matched event</param>
		/// <param name="Level">The log level</param>
		/// <param name="EventId">Id to associate with the event</param>
		/// <returns>New log event</returns>
		public LogEvent ToLogEvent(LogEventPriority Priority, LogLevel Level, EventId EventId)
		{
			StringBuilder Format = new StringBuilder();

			// Remove any empty lines at the end of the event
			while (LineCollection.Count > 1)
			{
				LogEventLine Line = LineCollection.Last();
				if (Line.Spans.Count > 0 || !String.IsNullOrWhiteSpace(Line.Text))
				{
					break;
				}
				LineCollection.RemoveAt(LineCollection.Count - 1);
			}

			// Append all the text, escaping the sections between markup objects and adding any additional metadata to
			// the arguments array
			LogEventState State = new LogEventState();
			foreach (LogEventLine Line in LineCollection)
			{
				if (Format.Length > 0)
				{
					Format.Append('\n');
				}

				int NextOffset = 0;
				foreach (KeyValuePair<string, LogEventSpan> Pair in Line.Spans.OrderBy(x => x.Value.Offset))
				{
					LogEventSpan Span = Pair.Value;
					if (Span.Offset >= NextOffset)
					{
						string Key = Pair.Key;
						for(int Idx = 2; State.HasProperty(Key); Idx++)
						{
							Key = $"{Pair.Key}_{Idx}";
						}

						MessageTemplate.Escape(Line.Text.AsSpan(NextOffset, Span.Offset - NextOffset), Format);
						Format.Append($"{{{Key}}}");
						State.AddProperty(Key, Span);
						NextOffset = Span.Offset + Span.Text.Length;
					}
				}
				MessageTemplate.Escape(Line.Text.AsSpan(NextOffset, Line.Text.Length - NextOffset), Format);
			}
			State.Format = Format.ToString();

			// Add the additional properties
			if (AdditionalProperties != null)
			{
				foreach (KeyValuePair<string, object> AdditionalProperty in AdditionalProperties)
				{
					State.AddProperty(AdditionalProperty.Key, AdditionalProperty.Value);
				}
			}

			// Write the data to the logger
			return new LogEvent(Priority, Level, EventId, Cursor.CurrentLineNumber, Cursor.CurrentLineNumber + MaxOffset, State);
		}
	}

	/// <summary>
	/// Extension methods for ILogCursor
	/// </summary>
	static class LogCursorExtensions
	{
		/// <summary>
		/// Creates a log event from the current cursor line
		/// </summary>
		/// <param name="Cursor">The current cursor</param>
		/// <param name="Priority">Priority of the event</param>
		/// <param name="Level">Log level for the issue</param>
		/// <param name="EventId">Known event id</param>
		/// <returns>The log event object</returns>
		public static LogEvent ToLogEvent(this ILogCursor Cursor, LogEventPriority Priority, LogLevel Level, EventId EventId)
		{
			return new LogEventBuilder(Cursor).ToLogEvent(Priority, Level, EventId);
		}
	}
}
