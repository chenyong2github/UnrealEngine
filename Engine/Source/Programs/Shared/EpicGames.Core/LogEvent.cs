// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Extensions.Logging;

namespace EpicGames.Core
{
	/// <summary>
	/// Static read-only utf8 strings for parsing log events
	/// </summary>
	public static class LogEventPropertyName
	{
		public static readonly Utf8String Time = new Utf8String("time");
		public static readonly Utf8String Level = new Utf8String("level");
		public static readonly Utf8String Id = new Utf8String("id");
		public static readonly Utf8String Line = new Utf8String("line");
		public static readonly Utf8String LineCount = new Utf8String("lineCount");
		public static readonly Utf8String Message = new Utf8String("message");
		public static readonly Utf8String Format = new Utf8String("format");
		public static readonly Utf8String Properties = new Utf8String("properties");

		public static readonly Utf8String Type = new Utf8String("$type");
		public static readonly Utf8String Text = new Utf8String("$text");

		public static readonly Utf8String Exception = new Utf8String("exception");
		public static readonly Utf8String Trace = new Utf8String("trace");
		public static readonly Utf8String InnerException = new Utf8String("innerException");
		public static readonly Utf8String InnerExceptions = new Utf8String("innerExceptions");
	}

	/// <summary>
	/// Epic representation of a log event. Can be serialized to/from Json for the Horde dashboard, and passed directly through ILogger interfaces.
	/// </summary>
	[JsonConverter(typeof(LogEventConverter))]
	public class LogEvent : IEnumerable<KeyValuePair<string, object?>>
	{
		static class InternalPropertyNames
		{
			public static string LineIndex = "$line";
			public static string LineCount = "$lineCount";
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
		public LogEvent(DateTime time, LogLevel level, EventId eventId, string message, string? format, Dictionary<string, object>? properties, LogException? exception)
			: this(time, level, eventId, 0, 1, message, format, properties, exception)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public LogEvent(DateTime time, LogLevel level, EventId eventId, int lineIndex, int lineCount, string message, string? format, Dictionary<string, object>? properties, LogException? exception)
		{
			Time = time;
			Level = level;
			Id = eventId;
			LineIndex = lineIndex;
			LineCount = lineCount;
			Message = message;
			Format = format;
			Properties = properties;
			Exception = exception;
		}

		/// <summary>
		/// Read a log event from a utf-8 encoded json byte array
		/// </summary>
		/// <param name="data"></param>
		/// <returns></returns>
		public static LogEvent Read(ReadOnlySpan<byte> data)
		{
			Utf8JsonReader reader = new Utf8JsonReader(data);
			reader.Read();
			return Read(ref reader);
		}

		/// <summary>
		/// Read a log event from Json
		/// </summary>
		/// <param name="reader">The Json reader</param>
		/// <returns>New log event</returns>
		public static LogEvent Read(ref Utf8JsonReader reader)
		{
			DateTime time = new DateTime(0);
			LogLevel level = LogLevel.None;
			EventId eventId = new EventId(0);
			int line = 0;
			int lineCount = 1;
			string message = String.Empty;
			string format = String.Empty;
			Dictionary<string, object>? properties = null;
			LogException? exception = null;

			ReadOnlySpan<byte> propertyName;
			for (; JsonExtensions.TryReadNextPropertyName(ref reader, out propertyName); reader.Skip())
			{
				if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Time.Span))
				{
					time = reader.GetDateTime();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Level.Span))
				{
					level = Enum.Parse<LogLevel>(reader.GetString());
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Id.Span))
				{
					eventId = reader.GetInt32();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Line.Span))
				{
					line = reader.GetInt32();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.LineCount.Span))
				{
					lineCount = reader.GetInt32();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Message.Span))
				{
					message = reader.GetString();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Format.Span))
				{
					format = reader.GetString();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Properties.Span))
				{
					properties = ReadProperties(ref reader);
				}
			}

			return new LogEvent(time, level, eventId, line, lineCount, message, format, properties, exception);
		}

		static Dictionary<string, object> ReadProperties(ref Utf8JsonReader reader)
		{
			Dictionary<string, object> properties = new Dictionary<string, object>();

			ReadOnlySpan<byte> propertyName;
			for (; JsonExtensions.TryReadNextPropertyName(ref reader, out propertyName); reader.Skip())
			{
				string name = Encoding.UTF8.GetString(propertyName);
				object value = ReadPropertyValue(ref reader);
				properties.Add(name, value);
			}

			return properties;
		}

		static object ReadPropertyValue(ref Utf8JsonReader reader)
		{
			switch (reader.TokenType)
			{
				case JsonTokenType.True:
					return true;
				case JsonTokenType.False:
					return true;
				case JsonTokenType.StartObject:
					return ReadStructuredPropertyValue(ref reader);
				case JsonTokenType.String:
					return reader.GetString();
				case JsonTokenType.Number:
					return reader.GetInt32();
				default:
					throw new InvalidOperationException("Unhandled property type");
			}
		}

		static LogValue ReadStructuredPropertyValue(ref Utf8JsonReader reader)
		{
			string type = String.Empty;
			string text = String.Empty;
			Dictionary<Utf8String, object>? properties = null;

			ReadOnlySpan<byte> propertyName;
			for (; JsonExtensions.TryReadNextPropertyName(ref reader, out propertyName); reader.Skip())
			{
				if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Type.Span))
				{
					type = reader.GetString();
				}
				else if (Utf8StringComparer.OrdinalIgnoreCase.Equals(propertyName, LogEventPropertyName.Text.Span))
				{
					text = reader.GetString();
				}
				else
				{
					properties ??= new Dictionary<Utf8String, object>();
					properties.Add(new Utf8String(propertyName.ToArray()), ReadPropertyValue(ref reader));
				}
			}

			return new LogValue(type, text, properties);
		}

		/// <summary>
		/// Writes a log event to Json
		/// </summary>
		/// <param name="writer"></param>
		public void Write(Utf8JsonWriter writer)
		{
			writer.WriteStartObject();
			writer.WriteString(LogEventPropertyName.Time.Span, Time.ToString("s", CultureInfo.InvariantCulture));
			writer.WriteString(LogEventPropertyName.Level.Span, Level.ToString());
			writer.WriteString(LogEventPropertyName.Message.Span, Message);

			if (Id.Id != 0)
			{
				writer.WriteNumber(LogEventPropertyName.Id.Span, Id.Id);
			}

			if (LineIndex > 0)
			{
				writer.WriteNumber(LogEventPropertyName.Line.Span, LineIndex);
			}

			if (LineCount > 1)
			{
				writer.WriteNumber(LogEventPropertyName.LineCount.Span, LineCount);
			}

			if (Format != null)
			{
				writer.WriteString(LogEventPropertyName.Format.Span, Format);
			}

			if (Properties != null && Properties.Any())
			{
				writer.WriteStartObject(LogEventPropertyName.Properties.Span);
				foreach ((string name, object? value) in Properties!)
				{
					writer.WritePropertyName(name);
					WritePropertyValue(ref writer, value);
				}
				writer.WriteEndObject();
			}

			if (Exception != null)
			{
				writer.WriteStartObject(LogEventPropertyName.Exception.Span);
				WriteException(ref writer, Exception);
				writer.WriteEndObject();
			}
			writer.WriteEndObject();
		}

		/// <summary>
		/// Write a property value to a json object
		/// </summary>
		/// <param name="writer"></param>
		/// <param name="value"></param>
		static void WritePropertyValue(ref Utf8JsonWriter writer, object? value)
		{
			if (value == null)
			{
				writer.WriteNullValue();
			}
			else
			{
				Type valueType = value.GetType();
				if (valueType == typeof(LogValue))
				{
					WriteStructuredPropertyValue(ref writer, (LogValue)value);
				}
				else
				{
					WriteSimplePropertyValue(ref writer, value, valueType);
				}
			}
		}

		static void WriteStructuredPropertyValue(ref Utf8JsonWriter writer, LogValue value)
		{
			writer.WriteStartObject();
			writer.WriteString(LogEventPropertyName.Type.Span, value.Type);
			writer.WriteString(LogEventPropertyName.Text.Span, value.Text);
			if (value.Properties != null)
			{
				foreach ((Utf8String propertyName, object? propertyValue) in value.Properties)
				{
					writer.WritePropertyName(propertyName);
					WritePropertyValue(ref writer, propertyValue);
				}
			}
			writer.WriteEndObject();
		}

		static void WriteSimplePropertyValue(ref Utf8JsonWriter writer, object value, Type valueType)
		{
			switch (Type.GetTypeCode(valueType))
			{
				case TypeCode.Boolean:
					writer.WriteBooleanValue((bool)value);
					break;
				case TypeCode.Byte:
					writer.WriteNumberValue((byte)value);
					break;
				case TypeCode.SByte:
					writer.WriteNumberValue((sbyte)value);
					break;
				case TypeCode.UInt16:
					writer.WriteNumberValue((ushort)value);
					break;
				case TypeCode.UInt32:
					writer.WriteNumberValue((uint)value);
					break;
				case TypeCode.UInt64:
					writer.WriteNumberValue((long)value);
					break;
				case TypeCode.Int16:
					writer.WriteNumberValue((short)value);
					break;
				case TypeCode.Int32:
					writer.WriteNumberValue((int)value);
					break;
				case TypeCode.Int64:
					writer.WriteNumberValue((long)value);
					break;
				case TypeCode.Decimal:
					writer.WriteNumberValue((decimal)value);
					break;
				case TypeCode.Double:
					writer.WriteNumberValue((double)value);
					break;
				case TypeCode.Single:
					writer.WriteNumberValue((float)value);
					break;
				case TypeCode.String:
					writer.WriteStringValue((string)value);
					break;
				default:
					writer.WriteStringValue(value.ToString() ?? String.Empty);
					break;
			}
		}

		/// <summary>
		/// Writes an exception to a json object
		/// </summary>
		/// <param name="writer">Writer to receive the exception data</param>
		/// <param name="exception">The exception</param>
		static void WriteException(ref Utf8JsonWriter writer, LogException exception)
		{
			writer.WriteString("message", exception.Message);
			writer.WriteString("trace", exception.Trace);

			if (exception.InnerException != null)
			{
				writer.WriteStartObject("innerException");
				WriteException(ref writer, exception.InnerException);
				writer.WriteEndObject();
			}

			if (exception.InnerExceptions != null)
			{
				writer.WriteStartArray("innerExceptions");
				for (int idx = 0; idx < 16 && idx < exception.InnerExceptions.Count; idx++) // Cap number of exceptions returned to avoid huge messages
				{
					LogException innerException = exception.InnerExceptions[idx];
					writer.WriteStartObject();
					WriteException(ref writer, innerException);
					writer.WriteEndObject();
				}
				writer.WriteEndArray();
			}
		}

		public static LogEvent Create(LogLevel level, string format, params object[] args)
			=> Create(level, KnownLogEvents.None, null, format, args);

		public static LogEvent Create(LogLevel level, EventId eventId, string format, params object[] args)
			=> Create(level, eventId, null, format, args);

		public static LogEvent Create(LogLevel level, EventId eventId, Exception? exception, string format, params object[] args)
		{
			Dictionary<string, object> properties = new Dictionary<string, object>();
			MessageTemplate.ParsePropertyValues(format, args, properties);

			string message = MessageTemplate.Render(format, properties!);
			return new LogEvent(DateTime.UtcNow, level, eventId, message, format, properties, LogException.FromException(exception));
		}

		/// <summary>
		/// Creates a log event from an ILogger parameters
		/// </summary>
		public static LogEvent FromState<TState>(LogLevel level, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
		{
			if(state is LogEvent logEvent)
			{
				return logEvent;
			}

			DateTime time = DateTime.UtcNow;

			// Render the message
			string message = formatter(state, exception);

			// Try to log the event
			IEnumerable<KeyValuePair<string, object>>? values = state as IEnumerable<KeyValuePair<string, object>>;
			if (values != null)
			{
				KeyValuePair<string, object>? format = values.FirstOrDefault(x => x.Key.Equals(MessageTemplate.FormatPropertyName, StringComparison.Ordinal));
				if (format != null)
				{
					// Format all the other values
					string formatString = format.Value.Value?.ToString() ?? String.Empty;
					Dictionary<string, object>? properties = MessageTemplate.CreatePositionalProperties(formatString, values);
					return new LogEvent(time, level, eventId, message, formatString, properties, LogException.FromException(exception));
				}
				else
				{
					// Include all the data, but don't use the format string
					return new LogEvent(time, level, eventId, message, null, values.ToDictionary(x => x.Key, x => x.Value), LogException.FromException(exception));
				}
			}
			else
			{
				// Format as a string
				return new LogEvent(time, level, eventId, message, null, null, LogException.FromException(exception));
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
				foreach ((string name, object? value) in Properties)
				{
					yield return new KeyValuePair<string, object?>(name, value?.ToString());
				}
			}
		}

		/// <summary>
		/// Enumerates all the properties in this object
		/// </summary>
		/// <returns>Property pairs</returns>
		System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator()
		{
			foreach (KeyValuePair<string, object?> pair in this)
			{
				yield return pair;
			}
		}

		/// <summary>
		/// Serialize a message template to JOSN
		/// </summary>
		public byte[] ToJsonBytes()
		{
			ArrayBufferWriter<byte> buffer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter writer = new Utf8JsonWriter(buffer))
			{
				Write(writer);
			}
			return buffer.WrittenSpan.ToArray();
		}

		/// <summary>
		/// Serialize a message template to JOSN
		/// </summary>
		public string ToJson()
		{
			ArrayBufferWriter<byte> buffer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter writer = new Utf8JsonWriter(buffer))
			{
				Write(writer);
			}
			return Encoding.UTF8.GetString(buffer.WrittenSpan);
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
		public Utf8String Type { get; set; }

		/// <summary>
		/// Rendering of the value
		/// </summary>
		public string Text { get; set; }

		/// <summary>
		/// Properties associated with the value
		/// </summary>
		public Dictionary<Utf8String, object>? Properties { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="type">Type of the value</param>
		/// <param name="text">Rendering of the value as text</param>
		/// <param name="properties">Additional properties for this value</param>
		public LogValue(Utf8String type, string text, Dictionary<Utf8String, object>? properties = null)
		{
			Type = type;
			Text = text;
			Properties = properties;
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
		public List<LogException> InnerExceptions { get; set; } = new List<LogException>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="message"></param>
		/// <param name="trace"></param>
		public LogException(string message, string trace)
		{
			Message = message;
			Trace = trace;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="exception"></param>
		[return: NotNullIfNotNull("exception")]
		public static LogException? FromException(Exception? exception)
		{
			LogException? result = null;
			if (exception != null)
			{
				result = new LogException(exception.Message, exception.StackTrace ?? String.Empty);

				if (exception.InnerException != null)
				{
					result.InnerException = FromException(exception.InnerException);
				}

				AggregateException? aggregateException = exception as AggregateException;
				if (aggregateException != null && aggregateException.InnerExceptions.Count > 0)
				{
					result.InnerExceptions = new List<LogException>();
					for (int idx = 0; idx < 16 && idx < aggregateException.InnerExceptions.Count; idx++) // Cap number of exceptions returned to avoid huge messages
					{
						LogException innerException = FromException(aggregateException.InnerExceptions[idx]);
						result.InnerExceptions.Add(innerException);
					}
				}
			}
			return result;
		}
	}

	/// <summary>
	/// Converter for serialization of <see cref="LogEvent"/> instances to Json streams
	/// </summary>
	public class LogEventConverter : JsonConverter<LogEvent>
	{
		/// <inheritdoc/>
		public override LogEvent Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
		{
			return LogEvent.Read(ref reader);
		}

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter writer, LogEvent value, JsonSerializerOptions options)
		{
			value.Write(writer);
		}
	}
}
