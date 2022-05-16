// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Text;
using System.Text.Json;
using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Utility
{
	class PerforceLogger : ILogger
	{
		readonly ILogger _inner;
		readonly DirectoryReference _baseDir;
		readonly PerforceViewMap? _viewMap;
		readonly int? _viewChange;

		public PerforceLogger(ILogger inner, DirectoryReference baseDir, PerforceViewMap? viewMap, int? viewChange)
		{
			_inner = inner;
			_baseDir = baseDir;
			_viewMap = viewMap;
			_viewChange = viewChange;
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception exception, Func<TState, Exception?, string> formatter)
		{
			JsonLogEvent logEvent = JsonLogEvent.FromLoggerState(logLevel, eventId, state, exception, formatter);
			logEvent = new JsonLogEvent(logLevel, eventId, Annotate(logEvent.Data));
			_inner.Log(logLevel, eventId, logEvent, null, JsonLogEvent.Format);
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel logLevel) => _inner.IsEnabled(logLevel);

		/// <inheritdoc/>
		public IDisposable BeginScope<TState>(TState state) => _inner.BeginScope(state);

		static bool ReadFirstLogProperty(ref Utf8JsonReader reader)
		{
			// Enter the main object
			if (!reader.Read() || reader.TokenType != JsonTokenType.StartObject)
			{
				return false;
			}

			// Find the 'properties' property
			for (; ; )
			{
				if (!reader.Read() || reader.TokenType != JsonTokenType.PropertyName)
				{
					return false;
				}
				if (reader.ValueTextEquals(LogEventPropertyName.Properties))
				{
					break;
				}
				reader.Skip();
			}

			// Enter the properties object
			if (!reader.Read() || reader.TokenType != JsonTokenType.StartObject)
			{
				return false;
			}

			// Find the first structured property object
			return ReadNextLogProperty(ref reader);
		}

		static bool ReadNextLogProperty(ref Utf8JsonReader reader)
		{
			for (; ; )
			{
				// Move to the next property name
				if (!reader.Read() || reader.TokenType != JsonTokenType.PropertyName)
				{
					return false;
				}

				// Move to the next property value
				if (!reader.Read())
				{
					return false;
				}

				// If it's an object, enter it
				if (reader.TokenType == JsonTokenType.StartObject)
				{
					return true;
				}

				// Otherwise skip this value
				reader.Skip();
			}
		}

		static readonly Utf8String s_sourceFileType = "SourceFile";
		static readonly Utf8String s_assetType = "Asset";

		static readonly Utf8String s_file = "file";

		ReadOnlyMemory<byte> Annotate(ReadOnlyMemory<byte> data)
		{
			ReadOnlyMemory<byte> output = data;
			int shift = 0;

			Utf8JsonReader reader = new Utf8JsonReader(data.Span);
			for (bool valid = ReadFirstLogProperty(ref reader); valid; valid = ReadNextLogProperty(ref reader))
			{
				ReadOnlySpan<byte> type = ReadOnlySpan<byte>.Empty;
				ReadOnlySpan<byte> text = ReadOnlySpan<byte>.Empty;
				string? file = null;

				// Get the $type and $text properties
				while (reader.Read() && reader.TokenType == JsonTokenType.PropertyName)
				{
					if (type.Length == 0 && reader.ValueTextEquals(LogEventPropertyName.Type))
					{
						if (reader.Read() && reader.TokenType == JsonTokenType.String)
						{
							type = reader.GetUtf8String();
							continue;
						}
					}
					if (text.Length == 0 && reader.ValueTextEquals(LogEventPropertyName.Text))
					{
						if (reader.Read() && reader.TokenType == JsonTokenType.String)
						{
							text = reader.GetUtf8String();
							continue;
						}
					}
					if (file == null && reader.ValueTextEquals(s_file))
					{
						if (reader.Read() && reader.TokenType == JsonTokenType.String)
						{
							file = reader.GetString();
							continue;
						}
					}
					reader.Skip();
				}

				// If we're at the end of the object, append any additional data
				byte[]? annotationBytes = GetAnnotations(type, text, file);
				if (annotationBytes != null)
				{
					int position = (int)reader.TokenStartIndex + shift;

					byte[] newOutput = new byte[output.Length + annotationBytes.Length];
					output.Span.Slice(0, position).CopyTo(newOutput);
					annotationBytes.CopyTo(newOutput.AsSpan(position));
					output.Span.Slice(position).CopyTo(newOutput.AsSpan(position + annotationBytes.Length));

					output = newOutput;
					shift += annotationBytes.Length;
				}
			}

			return output;
		}

		byte[]? GetAnnotations(ReadOnlySpan<byte> type, ReadOnlySpan<byte> text, string? file)
		{
			if (type.SequenceEqual(s_sourceFileType) || type.SequenceEqual(s_assetType))
			{
				StringBuilder annotations = new StringBuilder();

				file ??= Encoding.UTF8.GetString(text);

				FileReference location = FileReference.Combine(_baseDir, file.Replace('\\', Path.DirectorySeparatorChar));
				if (location.IsUnderDirectory(_baseDir))
				{
					string relativePath = location.MakeRelativeTo(_baseDir).Replace('\\', '/');
					annotations.Append($",\"relativePath\":\"{JsonEncodedText.Encode(relativePath)}\"");

					if (_viewMap != null && _viewMap.TryMapFile(relativePath, StringComparison.OrdinalIgnoreCase, out string depotFile))
					{
						if (_viewChange != null)
						{
							depotFile = $"{depotFile}@{_viewChange}";
						}
						annotations.Append($",\"depotPath\":\"{JsonEncodedText.Encode(depotFile)}\"");
					}
				}

				return Encoding.UTF8.GetBytes(annotations.ToString());
			}
			return null;
		}
	}
}
