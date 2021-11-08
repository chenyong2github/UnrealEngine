// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace EpicGames.Core
{
	/// <summary>
	/// Stores information about a span within a line
	/// </summary>
	public class LogEventSpan
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
	public class LogEventLine
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
	public class LogEventBuilder
	{
		class LogSpan
		{
			public string Name;
			public int Offset;
			public int Length;
			public object? Value;

			public LogSpan(string Name, int Offset, int Length, object? Value)
			{
				this.Name = Name;
				this.Offset = Offset;
				this.Length = Length;
				this.Value = Value;
			}
		}

		class LogLine
		{
			public string Message;
			public string? Format;
			public Dictionary<string, object>? Properties;

			public LogLine(string Message, string? Format, Dictionary<string, object>? Properties)
			{
				this.Message = Message;
				this.Format = Format;
				this.Properties = Properties;
			}
		}

		/// <summary>
		/// The current cursor position
		/// </summary>
		public ILogCursor Current { get; private set; }

		/// <summary>
		/// The next cursor position
		/// </summary>
		public ILogCursor Next { get; private set; }

		/// <summary>
		/// Events which have been parsed so far
		/// </summary>
		List<LogLine>? Lines;

		/// <summary>
		/// Spans for the current line
		/// </summary>
		List<LogSpan>? Spans;

		/// <summary>
		/// Additional properties for this line
		/// </summary>
		Dictionary<string, object>? Properties;

		/// <summary>
		/// Starts building a log event at the current cursor position
		/// </summary>
		/// <param name="Cursor">The current cursor position</param>
		public LogEventBuilder(ILogCursor Cursor, int LineCount = 1)
		{
			this.Current = Cursor;
			this.Next = Cursor.Rebase(1);

			if (LineCount > 1)
			{
				MoveNext(LineCount - 1);
			}
		}

		/// <summary>
		/// Creates a log event from the current line
		/// </summary>
		/// <returns></returns>
		LogLine CreateLine()
		{
			int Offset = 0;
			string CurrentLine = Current.CurrentLine!;

			string? Format = null;
			if (Spans != null)
			{
				StringBuilder Builder = new StringBuilder();
				foreach (LogSpan Span in Spans)
				{
					if (Span.Offset >= Offset)
					{
						Builder.Append(CurrentLine, Offset, Span.Offset - Offset);
						Builder.Append($"{{{Span.Name}}}");
						Offset = Span.Offset + Span.Length;
					}
				}
				Builder.Append(CurrentLine, Offset, CurrentLine.Length - Offset);
				Format = Builder.ToString();
			}

			return new LogLine(CurrentLine, Format, Properties);
		}

		/// <summary>
		/// Adds a span containing markup on the source text
		/// </summary>
		/// <param name="Name">Name to use to identify the item in the format string</param>
		/// <param name="Offset">Offset within the line</param>
		/// <param name="Length">Length of the span</param>
		/// <param name="Data">Data of the span</param>
		/// <returns>New span for the given range</returns>
		public void Annotate(string Name, int Offset, int Length, object? Value = null)
		{
			LogSpan Span = new LogSpan(Name, Offset, Length, Value);

			this.Properties ??= new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
			this.Properties.Add(Name, Span);

			Spans ??= new List<LogSpan>();
			for(int InsertIdx = Spans.Count; ;InsertIdx--)
			{
				if (InsertIdx == 0 || Spans[InsertIdx - 1].Offset < Offset)
				{
					Spans.Insert(InsertIdx, Span);
					break;
				}
			}
		}

		/// <summary>
		/// Adds a span containing markup for a regex match group
		/// </summary>
		/// <param name="Group">The match group</param>
		/// <param name="Name">Name to use to identify the item in the format string</param>
		public void Annotate(string Name, Group Group, object? Value = null)
		{
			Annotate(Name, Group.Index, Group.Length, Value);
		}

		/// <summary>
		/// Adds a span naming a regex match group, using the name of the group
		/// </summary>
		/// <param name="Group">The match group</param>
		public void Annotate(Group Group, object? Value = null)
		{
			Annotate(Group.Name, Group.Index, Group.Length, Value);
		}

		/// <summary>
		/// Adds a span naming a regex match group, using the name of the group
		/// </summary>
		/// <param name="Group">The match group</param>
		/// <param name="Name">Name to use to identify the item in the format string</param>
		public bool TryAnnotate(string Name, Group Group, object? Value = null)
		{
			if (Group.Success)
			{
				Annotate(Name, Group);
				return true;
			}
			return false;
		}

		/// <summary>
		/// Adds a span naming a regex match group, using the name of the group
		/// </summary>
		/// <param name="Group">The match group</param>
		public bool TryAnnotate(Group Group, object? Value = null)
		{
			if (Group.Success)
			{
				Annotate(Group, Value);
				return true;
			}
			return false;
		}

		/// <summary>
		/// Adds an additional named property
		/// </summary>
		/// <param name="Name">Name of the argument</param>
		/// <param name="Value">Value to associate with it</param>
		public void AddProperty(string Name, object Value)
		{
			Properties ??= new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
			Properties.Add(Name, Value);
		}

		/// <summary>
		/// Complete the current line and move to the next
		/// </summary>
		public void MoveNext()
		{
			Lines ??= new List<LogLine>();
			Lines.Add(CreateLine());

			Spans = null;
			Properties = null;

			Current = Next;
			Next = Next.Rebase(1);
		}

		/// <summary>
		/// Advance by the given number of lines
		/// </summary>
		/// <param name="Count"></param>
		public void MoveNext(int Count)
		{
			for (int Idx = 0; Idx < Count; Idx++)
			{
				MoveNext();
			}
		}

		/// <summary>
		/// Returns an array of log events
		/// </summary>
		/// <returns></returns>
		public LogEvent[] ToArray(LogLevel Level, EventId EventId)
		{
			DateTime Time = DateTime.UtcNow;

			int NumLines = Lines?.Count ?? 0;
			int NumEvents = NumLines;
			if (Current.CurrentLine != null)
			{
				NumEvents++;
			}

			LogEvent[] Events = new LogEvent[NumEvents];
			for (int Idx = 0; Idx < NumLines; Idx++)
			{
				Events[Idx] = CreateEvent(Time, Level, EventId, Idx, NumEvents, Lines![Idx]);
			}
			if (Current.CurrentLine != null)
			{
				Events[NumEvents - 1] = CreateEvent(Time, Level, EventId, NumEvents - 1, NumEvents, CreateLine());
			}

			return Events;
		}

		static LogEvent CreateEvent(DateTime Time, LogLevel Level, EventId EventId, int LineIndex, int LineCount, LogLine Line)
		{
			Dictionary<string, object>? Properties = null;
			if (Line.Properties != null)
			{
				Properties = new Dictionary<string, object>();
				foreach ((string Name, object Value) in Line.Properties)
				{
					object NewValue;
					if (Value is LogSpan Span)
					{
						string Text = Line.Message.Substring(Span.Offset, Span.Length);
						if (Span.Value == null)
						{
							NewValue = Text;
						}
						else
						{
							NewValue = LogEventFormatter.Format(Span.Value!);
							if (NewValue is LogValue Event)
							{
								Event.Text = Text;
							}
						}
					}
					else
					{
						NewValue = LogEventFormatter.Format(Value);
					}
					Properties[Name] = NewValue;
				}
			}
			return new LogEvent(Time, Level, EventId, LineIndex, LineCount, Line.Message, Line.Format, Properties, null);
		}

		/// <summary>
		/// Creates a match object at the given priority
		/// </summary>
		/// <param name="Level"></param>
		/// <param name="EventId">The event id</param>
		/// <param name="Priority"></param>
		/// <returns></returns>
		public LogEventMatch ToMatch(LogEventPriority Priority, LogLevel Level, EventId EventId)
		{
			return new LogEventMatch(Priority, ToArray(Level, EventId));
		}
	}
}
