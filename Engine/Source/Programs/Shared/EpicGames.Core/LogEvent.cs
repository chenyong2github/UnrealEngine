// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace EpicGames.Core
{
	/// <summary>
	/// Epic representation of a log event. Can be serialized to/from Json for the Horde dashboard, and passed directly through ILogger interfaces.
	/// </summary>
	public class LogEvent : IEnumerable<KeyValuePair<string, object?>>
	{
		static class InternalPropertyNames
		{
			public static string LineIndex = "$line";
			public static string LineCount = "$lineCount";
		}

		static class JsonPropertyNames
		{
			public static Utf8String Time { get; } = new Utf8String("time");
			public static Utf8String Level { get; } = new Utf8String("level");
			public static Utf8String Id { get; } = new Utf8String("id");
			public static Utf8String Line { get; } = new Utf8String("line");
			public static Utf8String LineCount { get; } = new Utf8String("lineCount");
			public static Utf8String Message { get; } = new Utf8String("message");
			public static Utf8String Format { get; } = new Utf8String("format");
			public static Utf8String Properties { get; } = new Utf8String("properties");

			public static Utf8String Type { get; } = new Utf8String("$type");
			public static Utf8String Text { get; } = new Utf8String("$text");

			public static Utf8String Exception { get; } = new Utf8String("exception");
			public static Utf8String Trace { get; } = new Utf8String("trace");
			public static Utf8String InnerException { get; } = new Utf8String("innerException");
			public static Utf8String InnerExceptions { get; } = new Utf8String("innerExceptions");
		}

		/// <summary>
		/// Time that the event was emitted
		/// </summary>
		public DateTime Time { get; set; }

		/// <summary>
		/// The log level
		/// </summary>
		public LogLevel Level { get; set; }

		/// <summary>
		/// Unique id associated with this event. See <see cref="KnownLogEvents"/> for possible values.
		/// </summary>
		public EventId Id { get; set; }

		/// <summary>
		/// Index of the line within a multi-line message
		/// </summary>
		public int LineIndex { get; set; }

		/// <summary>
		/// Number of lines in the message
		/// </summary>
		public int LineCount { get; set; }

		/// <summary>
		/// The formatted message
		/// </summary>
		public string Message { get; set; }

		/// <summary>
		/// Message template string
		/// </summary>
		public string? Format { get; set; }

		/// <summary>
		/// Map of property name to value
		/// </summary>
		public Dictionary<string, object>? Properties { get; set; }

		/// <summary>
		/// The exception value
		/// </summary>
		public LogException? Exception { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public LogEvent(DateTime Time, LogLevel Level, EventId EventId, string Message, string? Format, Dictionary<string, object>? Properties, LogException? Exception)
			: this(Time, Level, EventId, 0, 1, Message, Format, Properties, Exception)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public LogEvent(DateTime Time, LogLevel Level, EventId EventId, int LineIndex, int LineCount, string Message, string? Format, Dictionary<string, object>? Properties, LogException? Exception)
		{
			this.Time = Time;
			this.Level = Level;
			this.Id = EventId;
			this.LineIndex = LineIndex;
			this.LineCount = LineCount;
			this.Message = Message;
			this.Format = Format;
			this.Properties = Properties;
			this.Exception = Exception;
		}

		/// <summary>
		/// Read a log event from Json
		/// </summary>
		/// <param name="Reader">The Json reader</param>
		/// <returns>New log event</returns>
		public static LogEvent Read(ref Utf8JsonReader Reader)
		{
			DateTime Time = new DateTime(0);
			LogLevel Level = LogLevel.None;
			EventId EventId = new EventId(0);
			int Line = 0;
			int LineCount = 1;
			string Message = String.Empty;
			string Format = String.Empty;
			Dictionary<string, object>? Properties = null;
			LogException? Exception = null;

			ReadOnlySpan<byte> PropertyName;
			for (; JsonExtensions.TryReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
			{
				if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Time.Span))
				{
					Time = Reader.GetDateTime();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Level.Span))
				{
					Level = Enum.Parse<LogLevel>(Reader.GetString());
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Id.Span))
				{
					EventId = Reader.GetInt32();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Line.Span))
				{
					Line = Reader.GetInt32();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.LineCount.Span))
				{
					LineCount = Reader.GetInt32();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Message.Span))
				{
					Message = Reader.GetString();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Format.Span))
				{
					Format = Reader.GetString();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Properties.Span))
				{
					Properties = ReadProperties(ref Reader);
				}
			}

			return new LogEvent(Time, Level, EventId, Line, LineCount, Message, Format, Properties, Exception);
		}

		static Dictionary<string, object> ReadProperties(ref Utf8JsonReader Reader)
		{
			Dictionary<string, object> Properties = new Dictionary<string, object>();

			ReadOnlySpan<byte> PropertyName;
			for (; JsonExtensions.TryReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
			{
				string Name = Encoding.UTF8.GetString(PropertyName);
				object Value = ReadPropertyValue(ref Reader);
				Properties.Add(Name, Value);
			}

			return Properties;
		}

		static object ReadPropertyValue(ref Utf8JsonReader Reader)
		{
			switch (Reader.TokenType)
			{
				case JsonTokenType.True:
					return true;
				case JsonTokenType.False:
					return true;
				case JsonTokenType.StartObject:
					return ReadStructuredPropertyValue(ref Reader);
				case JsonTokenType.String:
					return Reader.GetString();
				case JsonTokenType.Number:
					return Reader.GetInt32();
				default:
					throw new InvalidOperationException("Unhandled property type");
			}
		}

		static LogValue ReadStructuredPropertyValue(ref Utf8JsonReader Reader)
		{
			string Type = String.Empty;
			string Text = String.Empty;
			Dictionary<string, object>? Properties = null;

			ReadOnlySpan<byte> PropertyName;
			for (; JsonExtensions.TryReadNextPropertyName(ref Reader, out PropertyName); Reader.Skip())
			{
				if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Type.Span))
				{
					Type = Reader.GetString();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(PropertyName, JsonPropertyNames.Text.Span))
				{
					Text = Reader.GetString();
				}
				else
				{
					Properties ??= new Dictionary<string, object>();
					Properties.Add(Encoding.UTF8.GetString(PropertyName), ReadPropertyValue(ref Reader));
				}
			}

			return new LogValue(Type, Text, Properties);
		}

		/// <summary>
		/// Writes a log event to Json
		/// </summary>
		/// <param name="Writer"></param>
		public void Write(Utf8JsonWriter Writer)
		{
			Writer.WriteStartObject();
			Writer.WriteString(JsonPropertyNames.Time.Span, Time.ToString("s", CultureInfo.InvariantCulture));
			Writer.WriteString(JsonPropertyNames.Level.Span, Level.ToString());
			Writer.WriteString(JsonPropertyNames.Message.Span, Message);

			if (Id.Id != 0)
			{
				Writer.WriteNumber(JsonPropertyNames.Id.Span, Id.Id);
			}

			if (LineIndex > 0)
			{
				Writer.WriteNumber(JsonPropertyNames.Line.Span, LineIndex);
			}

			if (LineCount > 1)
			{
				Writer.WriteNumber(JsonPropertyNames.LineCount.Span, LineCount);
			}

			if (Format != null)
			{
				Writer.WriteString(JsonPropertyNames.Format.Span, Format);
			}

			if (Properties != null && Properties.Any())
			{
				Writer.WriteStartObject(JsonPropertyNames.Properties.Span);
				foreach ((string Name, object? Value) in Properties!)
				{
					Writer.WritePropertyName(Name);
					WritePropertyValue(ref Writer, Value);
				}
				Writer.WriteEndObject();
			}

			if (Exception != null)
			{
				Writer.WriteStartObject(JsonPropertyNames.Exception.Span);
				WriteException(ref Writer, Exception);
				Writer.WriteEndObject();
			}
			Writer.WriteEndObject();
		}

		/// <summary>
		/// Write a property value to a json object
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Value"></param>
		static void WritePropertyValue(ref Utf8JsonWriter Writer, object? Value)
		{
			if (Value == null)
			{
				Writer.WriteNullValue();
			}
			else
			{
				Type ValueType = Value.GetType();
				if (ValueType == typeof(LogValue))
				{
					WriteStructuredPropertyValue(ref Writer, (LogValue)Value);
				}
				else
				{
					WriteSimplePropertyValue(ref Writer, Value, ValueType);
				}
			}
		}

		static void WriteStructuredPropertyValue(ref Utf8JsonWriter Writer, LogValue Value)
		{
			Writer.WriteStartObject();
			Writer.WriteString(JsonPropertyNames.Type.Span, Value.Type);
			Writer.WriteString(JsonPropertyNames.Text.Span, Value.Text);
			if (Value.Properties != null)
			{
				foreach ((string PropertyName, object? PropertyValue) in Value.Properties)
				{
					Writer.WritePropertyName(PropertyName);
					WritePropertyValue(ref Writer, PropertyValue);
				}
			}
			Writer.WriteEndObject();
		}

		static void WriteSimplePropertyValue(ref Utf8JsonWriter Writer, object Value, Type ValueType)
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
					Writer.WriteStringValue(Value.ToString() ?? String.Empty);
					break;
			}
		}

		/// <summary>
		/// Writes an exception to a json object
		/// </summary>
		/// <param name="Writer">Writer to receive the exception data</param>
		/// <param name="Exception">The exception</param>
		static void WriteException(ref Utf8JsonWriter Writer, LogException Exception)
		{
			Writer.WriteString("message", Exception.Message);
			Writer.WriteString("trace", Exception.Trace);

			if (Exception.InnerException != null)
			{
				Writer.WriteStartObject("innerException");
				WriteException(ref Writer, Exception.InnerException);
				Writer.WriteEndObject();
			}

			if (Exception.InnerExceptions != null)
			{
				Writer.WriteStartArray("innerExceptions");
				for (int Idx = 0; Idx < 16 && Idx < Exception.InnerExceptions.Count; Idx++) // Cap number of exceptions returned to avoid huge messages
				{
					LogException InnerException = Exception.InnerExceptions[Idx];
					Writer.WriteStartObject();
					WriteException(ref Writer, InnerException);
					Writer.WriteEndObject();
				}
				Writer.WriteEndArray();
			}
		}

		/// <summary>
		/// Creates a log event from an ILogger parameters
		/// </summary>
		public static LogEvent FromState<TState>(LogLevel Level, EventId EventId, TState State, Exception? Exception, Func<TState, Exception?, string> Formatter)
		{
			DateTime Time = DateTime.UtcNow;

			// Render the message
			string Message = Formatter(State, Exception);

			// Try to log the event
			IEnumerable<KeyValuePair<string, object>>? Values = State as IEnumerable<KeyValuePair<string, object>>;
			if (Values != null)
			{
				KeyValuePair<string, object>? Format = Values.FirstOrDefault(x => x.Key.Equals(MessageTemplate.FormatPropertyName, StringComparison.Ordinal));
				if (Format != null)
				{
					// Format all the other values
					string FormatString = Format.Value.Value?.ToString() ?? String.Empty;
					Dictionary<string, object>? Properties = MessageTemplate.CreatePositionalProperties(FormatString, Values);
					return new LogEvent(Time, Level, EventId, Message, FormatString, Properties, LogException.FromException(Exception));
				}
				else
				{
					// Include all the data, but don't use the format string
					return new LogEvent(Time, Level, EventId, Message, null, Values.ToDictionary(x => x.Key, x => x.Value), LogException.FromException(Exception));
				}
			}
			else
			{
				// Format as a string
				return new LogEvent(Time, Level, EventId, Message, null, null, LogException.FromException(Exception));
			}
		}

		/// <summary>
		/// Enumerates all the properties in this object
		/// </summary>
		/// <returns>Property pairs</returns>
		public IEnumerator<KeyValuePair<string, object?>> GetEnumerator()
		{
			if (Format != null)
			{
				yield return new KeyValuePair<string, object?>(MessageTemplate.FormatPropertyName, Format.ToString());
			}

			if (LineIndex > 0)
			{
				yield return new KeyValuePair<string, object?>(InternalPropertyNames.LineIndex, LineIndex);
			}

			if (LineCount > 1)
			{
				yield return new KeyValuePair<string, object?>(InternalPropertyNames.LineCount, LineCount);
			}

			if (Properties != null)
			{
				foreach ((string Name, object? Value) in Properties)
				{
					yield return new KeyValuePair<string, object?>(Name, Value?.ToString());
				}
			}
		}

		/// <summary>
		/// Enumerates all the properties in this object
		/// </summary>
		/// <returns>Property pairs</returns>
		System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator()
		{
			foreach (KeyValuePair<string, object?> Pair in this)
			{
				yield return Pair;
			}
		}

		/// <summary>
		/// Serialize a message template to JOSN
		/// </summary>
		public byte[] ToJsonBytes()
		{
			ArrayBufferWriter<byte> Buffer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter Writer = new Utf8JsonWriter(Buffer))
			{
				Write(Writer);
			}
			return Buffer.WrittenSpan.ToArray();
		}

		/// <summary>
		/// Serialize a message template to JOSN
		/// </summary>
		public string ToJson()
		{
			ArrayBufferWriter<byte> Buffer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter Writer = new Utf8JsonWriter(Buffer))
			{
				Write(Writer);
			}
			return Encoding.UTF8.GetString(Buffer.WrittenSpan);
		}

		/// <inheritdoc/>
		public override string ToString() => Message;
	}

	/// <summary>
	/// Information for a structured value for use in log events
	/// </summary>
	public sealed class LogValue
	{
		/// <summary>
		/// Type of the event
		/// </summary>
		public string Type { get; set; }

		/// <summary>
		/// Rendering of the value
		/// </summary>
		public string Text { get; set; }

		/// <summary>
		/// Properties associated with the value
		/// </summary>
		public Dictionary<string, object>? Properties { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type">Type of the value</param>
		/// <param name="Text">Rendering of the value as text</param>
		/// <param name="Properties">Additional properties for this value</param>
		public LogValue(string Type, string Text, Dictionary<string, object>? Properties = null)
		{
			this.Type = Type;
			this.Text = Text;
			this.Properties = Properties;
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return Text;
		}
	}

	/// <summary>
	/// Information about an exception in a log event
	/// </summary>
	public sealed class LogException
	{
		/// <summary>
		/// Exception message
		/// </summary>
		public string Message { get; set; }

		/// <summary>
		/// Stack trace for the exception
		/// </summary>
		public string Trace { get; set; }

		/// <summary>
		/// Optional inner exception information
		/// </summary>
		public LogException? InnerException { get; set; }

		/// <summary>
		/// Multiple inner exceptions, in the case of an <see cref="AggregateException"/>
		/// </summary>
		public List<LogException> InnerExceptions = new List<LogException>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message"></param>
		/// <param name="Trace"></param>
		public LogException(string Message, string Trace)
		{
			this.Message = Message;
			this.Trace = Trace;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Exception"></param>
		[return: NotNullIfNotNull("Exception")]
		public static LogException? FromException(Exception? Exception)
		{
			LogException? Result = null;
			if (Exception != null)
			{
				Result = new LogException(Exception.Message, Exception.StackTrace ?? String.Empty);

				if (Exception.InnerException != null)
				{
					Result.InnerException = FromException(Exception.InnerException);
				}

				AggregateException? AggregateException = Exception as AggregateException;
				if (AggregateException != null && AggregateException.InnerExceptions.Count > 0)
				{
					Result.InnerExceptions = new List<LogException>();
					for (int Idx = 0; Idx < 16 && Idx < AggregateException.InnerExceptions.Count; Idx++) // Cap number of exceptions returned to avoid huge messages
					{
						LogException InnerException = FromException(AggregateException.InnerExceptions[Idx]);
						Result.InnerExceptions.Add(InnerException);
					}
				}
			}
			return Result;
		}
	}
}
