// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Defines a preformatted Json log event, which can pass through raw Json data directly or format it as a regular string
	/// </summary>
	public struct JsonLogEvent
	{
		static Utf8String LevelString { get; } = new Utf8String("Level");
		static Utf8String PropertiesString { get; } = new Utf8String("Properties");
		static Utf8String EventIdString { get; } = new Utf8String("EventId");
		static Utf8String IdString { get; } = new Utf8String("Id");
		static Utf8String RenderedMessageString { get; } = new Utf8String("RenderedMessage");

		/// <summary>
		/// The 
		/// </summary>
		public LogLevel Level { get; }

		/// <summary>
		/// The event id, if set
		/// </summary>
		public EventId EventId { get; }

		/// <summary>
		/// The utf-8 encoded JSON event
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public JsonLogEvent(LogLevel Level, EventId EventId, ReadOnlyMemory<byte> Data)
		{
			this.Level = Level;
			this.EventId = EventId;
			this.Data = Data;
		}

		/// <summary>
		/// Tries to parse a Json log event from the given 
		/// </summary>
		/// <param name="Data"></param>
		/// <param name="Event"></param>
		/// <returns></returns>
		public static bool TryParse(ReadOnlyMemory<byte> Data, out JsonLogEvent Event)
		{
			LogLevel Level = LogLevel.None;
			int EventId = 0;

			Utf8JsonReader Reader = new Utf8JsonReader(Data.Span);
			if (Reader.Read() && Reader.TokenType == JsonTokenType.StartObject)
			{
				ReadOnlySpan<byte> PropertyName;
				for (; ReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
				{
					if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, LevelString.Span))
					{
						if (!Enum.TryParse(Reader.GetString(), out Level))
						{
							break;
						}
					}
					else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, PropertiesString.Span) && Reader.TokenType == JsonTokenType.StartObject)
					{
						for (; ReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
						{
							if (Utf8StringComparer.OrdinalIgnoreCase.Equals(EventIdString.Span, PropertyName) && Reader.TokenType == JsonTokenType.StartObject)
							{
								for (; ReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
								{
									if (Utf8StringComparer.OrdinalIgnoreCase.Equals(IdString.Span, PropertyName) && Reader.TryGetInt32(out int NewEventId))
									{
										EventId = NewEventId;
									}
								}
							}
						}
					}
				}
			}

			if (Reader.TokenType == JsonTokenType.EndObject && Level != LogLevel.None && Reader.BytesConsumed == Data.Length)
			{
				Event = new JsonLogEvent(Level, new EventId(EventId), Data);
				return true;
			}
			else
			{
				Event = default;
				return false;
			}
		}

		static bool ReadNextPropertyName(ref Utf8JsonReader Reader, out ReadOnlySpan<byte> PropertyName)
		{
			if (Reader.Read() && Reader.TokenType == JsonTokenType.PropertyName)
			{
				PropertyName = Reader.ValueSpan;
				return Reader.Read();
			}
			else
			{
				PropertyName = ReadOnlySpan<byte>.Empty;
				return false;
			}
		}

		/// <summary>
		/// Formats an event as a string
		/// </summary>
		/// <param name="State"></param>
		/// <param name="Ex"></param>
		/// <returns></returns>
		/// <exception cref="NotImplementedException"></exception>
		public static string Format(JsonLogEvent State, Exception? Ex)
		{
			Utf8JsonReader Reader = new Utf8JsonReader(State.Data.Span);
			if (Reader.Read() && Reader.TokenType == JsonTokenType.StartObject)
			{
				for (; ; )
				{
					if (!Reader.Read() || Reader.TokenType != JsonTokenType.PropertyName)
					{
						break;
					}

					bool IsMessage = Utf8StringComparer.OrdinalIgnoreCase.Equals(Reader.ValueSpan, RenderedMessageString.Span);
					if (!Reader.TrySkip())
					{
						break;
					}
					if (IsMessage && Reader.TokenType == JsonTokenType.String)
					{
						return Reader.GetString();
					}
				}
			}
			return String.Empty;
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return Encoding.UTF8.GetString(Data.ToArray());
		}
	}

	/// <summary>
	/// Class to handle uploading log data to the server in the background
	/// </summary>
	public abstract class JsonLogger : ILogger
	{
		/// <summary>
		/// Whether the logger should include warnings, or downgrade them to information
		/// </summary>
		protected bool? Warnings { get; }

		/// <summary>
		/// The inner logger device
		/// </summary>
		protected ILogger Inner { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Warnings">Whether to include warnings in the output</param>
		/// <param name="Inner">Additional logger to write to</param>
		public JsonLogger(bool? Warnings, ILogger Inner)
		{
			this.Warnings = Warnings;
			this.Inner = Inner;
		}

		/// <inheritdoc/>
		public IDisposable? BeginScope<TState>(TState State)
		{
			return null;
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel LogLevel)
		{
			return true;
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel LogLevel, EventId EventId, TState State, Exception? Exception, Func<TState, Exception?, string> Formatter)
		{
			// Downgrade warnings to information if not required
			if (LogLevel == LogLevel.Warning && !(Warnings ?? true))
			{
				LogLevel = LogLevel.Information;
			}

			if(State is JsonLogEvent Event)
			{
				WriteFormattedEvent(LogLevel, new byte[][] { Event.Data.ToArray() });
				return;
			}

			// Render the message
			string Text = Formatter(State, Exception);

			// Try to log the event
			IEnumerable<KeyValuePair<string, object>>? Values = State as IEnumerable<KeyValuePair<string, object>>;
			if (Values != null)
			{
				KeyValuePair<string, object>? Format = Values.FirstOrDefault(x => x.Key.Equals(MessageTemplate.FormatPropertyName, StringComparison.Ordinal));
				if (Format != null)
				{
					// Format all the other values
					QueueFormattedEvent(LogLevel, EventId, Text, Format.Value.Value?.ToString() ?? String.Empty, Values.Where(x => !x.Key.Equals(MessageTemplate.FormatPropertyName, StringComparison.Ordinal)), Exception);
				}
				else
				{
					// Include all the data, but don't use the format string
					QueueFormattedEvent(LogLevel, EventId, Text, null, Values, Exception);
				}
			}
			else
			{
				// Format as a string
				QueueFormattedEvent(LogLevel, EventId, Text, null, null, Exception);
			}
		}

		/// <summary>
		/// Queues up a log event for the server
		/// </summary>
		/// <param name="LogLevel">Level for this log event</param>
		/// <param name="EventId">Corresponding event id</param>
		/// <param name="Text">Rendered text for the message</param>
		/// <param name="Format">Format string</param>
		/// <param name="Properties">Properties for the message</param>
		/// <param name="Exception">Exception information</param>
		void QueueFormattedEvent(LogLevel LogLevel, EventId EventId, string Text, string? Format, IEnumerable<KeyValuePair<string, object>>? Properties, Exception? Exception)
		{
			DateTime Time = DateTime.UtcNow;

			// Split it into individual lines
			string[] Lines = Text.Split('\n');

			// Split the format string into lines too
			string[]? FormatLines = null;
			if (Format != null)
			{
				FormatLines = Format.Split('\n');
			}

			// If it's a systemic issue, log it to the regular log sink so we can catch it in Datadog
			if (EventId.Id >= KnownLogEvents.Systemic.Id && EventId.Id < KnownLogEvents.Systemic.Id + 100)
			{
				WriteSystemicEvent(EventId, Text);
			}

			// In order to support multi-line log events while seeking through the log buffer, we re-output the same json event for each
			// line in the multi-line output. The 'line' parameter for the second and successive lines gives the number of this line.
			byte[][] OutputLines = new byte[Lines.Length][];
			for (int LineIdx = 0; LineIdx < Lines.Length; LineIdx++)
			{
				// Encode the data for this line
				string? LineFormat = null;
				if (FormatLines != null)
				{
					if (LineIdx < FormatLines.Length)
					{
						LineFormat = FormatLines[LineIdx];
					}
					else
					{
						LineFormat = "Missing format string";
					}
				}

				OutputLines[LineIdx] = EncodeMessage(Time, LogLevel, Lines[LineIdx], EventId, LineIdx, Lines.Length, LineFormat, Properties, Exception);
			}
			WriteFormattedEvent(LogLevel, OutputLines);
		}

		/// <summary>
		/// Writes a formatted event
		/// </summary>
		/// <param name="Level">The log level</param>
		/// <param name="Lines">Utf-8 encoded JSON line data</param>
		protected abstract void WriteFormattedEvent(LogLevel Level, byte[][] Lines);

		/// <summary>
		/// Callback to write a systemic event
		/// </summary>
		/// <param name="EventId">The event id</param>
		/// <param name="Text">The event text</param>
		protected virtual void WriteSystemicEvent(EventId EventId, string Text)
		{
			Inner.LogWarning("Systemic event {KnownLogEventId}: {Text}", EventId.Id, Text);
		}

		/// <summary>
		/// Queues up a log event for the server
		/// </summary>
		/// <param name="Time">Time of the event</param>
		/// <param name="LogLevel">Level for this log event</param>
		/// <param name="Text">Rendered text for the message</param>
		/// <param name="EventId">Corresponding event id</param>
		/// <param name="LineIndex">Line index within the event</param>
		/// <param name="LineCount">Number of lines in the event</param>
		/// <param name="Format">Format string</param>
		/// <param name="Properties">Properties for the message</param>
		/// <param name="Exception">Exception information</param>
		byte[] EncodeMessage(DateTime Time, LogLevel LogLevel, string Text, EventId EventId, int LineIndex, int LineCount, string? Format, IEnumerable<KeyValuePair<string, object>>? Properties, Exception? Exception)
		{
			ArrayBufferWriter<byte> Writer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter JsonWriter = new Utf8JsonWriter(Writer))
			{
				JsonWriter.WriteStartObject();
				JsonWriter.WriteString("time", Time.ToString("s", CultureInfo.InvariantCulture));
				JsonWriter.WriteString("level", LogLevel.ToString());
				JsonWriter.WriteString("message", Text);

				if (EventId != KnownLogEvents.None)
				{
					JsonWriter.WriteNumber("id", EventId.Id);
				}

				if (LineCount > 1)
				{
					JsonWriter.WriteNumber("line", LineIndex);
					JsonWriter.WriteNumber("lineCount", LineCount);
				}

				if (Format != null)
				{
					JsonWriter.WriteString("format", Format);
				}

				if (Properties != null && Properties.Any())
				{
					JsonWriter.WriteStartObject("properties");
					foreach (KeyValuePair<string, object> Pair in Properties!)
					{
						JsonWriter.WritePropertyName(Pair.Key);
						WritePropertyValue(Pair.Value, JsonWriter);
					}
					JsonWriter.WriteEndObject();
				}

				if (Exception != null)
				{
					JsonWriter.WriteStartObject("exception");
					WriteException(JsonWriter, Exception);
					JsonWriter.WriteEndObject();
				}

				JsonWriter.WriteEndObject();
			}
			return Writer.WrittenSpan.ToArray();
		}

		/// <summary>
		/// Writes an exception to a json object
		/// </summary>
		/// <param name="Writer">Writer to receive the exception data</param>
		/// <param name="Exception">The exception</param>
		void WriteException(Utf8JsonWriter Writer, Exception Exception)
		{
			Writer.WriteString("message", Exception.Message);
			Writer.WriteString("trace", Exception.StackTrace);

			if (Exception.InnerException != null)
			{
				Writer.WriteStartObject("innerException");
				WriteException(Writer, Exception.InnerException);
				Writer.WriteEndObject();
			}

			AggregateException? AggregateException = Exception as AggregateException;
			if (AggregateException != null)
			{
				Writer.WriteStartArray("innerExceptions");
				for (int Idx = 0; Idx < 16 && Idx < AggregateException.InnerExceptions.Count; Idx++) // Cap number of exceptions returned to avoid huge messages
				{
					Exception InnerException = AggregateException.InnerExceptions[Idx];
					Writer.WriteStartObject();
					WriteException(Writer, InnerException);
					Writer.WriteEndObject();
				}
				Writer.WriteEndArray();
			}
		}

		/// <summary>
		/// Converts a property value to log markup
		/// </summary>
		/// <param name="Value">The value to write</param>
		/// <param name="Writer">The json writer</param>
		static void WritePropertyValue(object Value, Utf8JsonWriter Writer)
		{
			if (Value == null)
			{
				Writer.WriteNullValue();
			}
			else
			{
				Type ValueType = Value.GetType();
				if (ValueType.IsEnum)
				{
					Writer.WriteStringValue(Enum.GetName(ValueType, Value));
				}
				else
				{
					switch (Type.GetTypeCode(ValueType))
					{
						case TypeCode.Boolean:
							Writer.WriteBooleanValue((bool)Value);
							break;
						case TypeCode.Byte:
							Writer.WriteNumberValue((Byte)Value);
							break;
						case TypeCode.SByte:
							Writer.WriteNumberValue((SByte)Value);
							break;
						case TypeCode.UInt16:
							Writer.WriteNumberValue((UInt16)Value);
							break;
						case TypeCode.UInt32:
							Writer.WriteNumberValue((UInt32)Value);
							break;
						case TypeCode.UInt64:
							Writer.WriteNumberValue((UInt64)Value);
							break;
						case TypeCode.Int16:
							Writer.WriteNumberValue((Int16)Value);
							break;
						case TypeCode.Int32:
							Writer.WriteNumberValue((Int32)Value);
							break;
						case TypeCode.Int64:
							Writer.WriteNumberValue((Int64)Value);
							break;
						case TypeCode.Decimal:
							Writer.WriteNumberValue((Decimal)Value);
							break;
						case TypeCode.Double:
							Writer.WriteNumberValue((Double)Value);
							break;
						case TypeCode.Single:
							Writer.WriteNumberValue((Single)Value);
							break;
						case TypeCode.String:
							Writer.WriteStringValue((String)Value);
							break;
						default:
							WriteComplexPropertyValue(Value, ValueType, Writer);
							break;
					}
				}
			}
		}

		/// <summary>
		/// Writes a complex object to the given json writer
		/// </summary>
		/// <param name="Value">The value to write</param>
		/// <param name="ValueType">Type of the value</param>
		/// <param name="JsonWriter">The writer to output to</param>
		static void WriteComplexPropertyValue(object Value, Type ValueType, Utf8JsonWriter JsonWriter)
		{
			if (ValueType == typeof(LogEventSpan))
			{
				LogEventSpan Span = (LogEventSpan)Value;
				if (Span.Properties.Count == 0)
				{
					JsonWriter.WriteStringValue(Span.Text);
				}
				else
				{
					JsonWriter.WriteStartObject();
					JsonWriter.WriteString("text", Span.Text);
					foreach (KeyValuePair<string, object> Pair in Span.Properties)
					{
						JsonWriter.WritePropertyName(Pair.Key);
						WritePropertyValue(Pair.Value, JsonWriter);
					}
					JsonWriter.WriteEndObject();
				}
			}
			else
			{
				JsonWriter.WriteStringValue(Value.ToString());
			}
		}
	}
}
