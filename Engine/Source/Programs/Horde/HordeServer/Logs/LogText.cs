// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.VisualBasic.CompilerServices;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// Interface for log data
	/// </summary>
	public interface ILogText
	{
		/// <summary>
		/// The raw text data. Contains a complete set of lines followed by newline characters.
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Length of the data in this block
		/// </summary>
		public int Length { get; }

		/// <summary>
		/// Offsets of lines within the data object, including a sentinel for the end of the data (LineCount + 1 entries).
		/// </summary>
		public IReadOnlyList<int> LineOffsets { get; }

		/// <summary>
		/// Number of lines in the 
		/// </summary>
		public int LineCount { get; }

		/// <summary>
		/// Computes the number of bytes used by this object
		/// </summary>
		public int AllocatedSize { get; }
	}

	/// <summary>
	/// Helper methods for parsing log text
	/// </summary>
	public static class LogTextHelpers
	{
		/// <summary>
		/// Finds the number of newline characters in the given span
		/// </summary>
		/// <param name="Span">Span of utf-8 characters to scan</param>
		/// <param name="Offset">Starting offset within the span</param>
		/// <returns>Number of lines in the given span</returns>
		public static int GetLineCount(ReadOnlySpan<byte> Span, int Offset)
		{
			int LineCount = 0;
			for (; Offset < Span.Length; Offset++)
			{
				if (Span[Offset] == (byte)'\n')
				{
					LineCount++;
				}
			}
			return LineCount;
		}

		/// <summary>
		/// Find the offsets of the start of each line within the given span
		/// </summary>
		/// <param name="Span">Span to search</param>
		/// <param name="Offset">Offset within the span to start searching</param>
		/// <param name="LineOffsets">Receives the list of line offsets</param>
		/// <param name="LineCount">The current line count</param>
		public static void FindLineOffsets(ReadOnlySpan<byte> Span, int Offset, Span<int> LineOffsets, int LineCount)
		{
			Debug.Assert(Span.Length == 0 || Span[Span.Length - 1] == (byte)'\n');
			for(; ; )
			{
				// Store the start of this line
				LineOffsets[LineCount++] = Offset;

				// Check if we're at the end
				if(Offset >= Span.Length)
				{
					break;
				}

				// Move to the end of this line
				while (Span[Offset] != '\n')
				{
					Offset++;
				}

				// Move to the start of the next line
				Offset++;
			}
		}
	}

	/// <summary>
	/// Immutable block of log text
	/// </summary>
	public class ReadOnlyLogText : ILogText
	{
		/// <inheritdoc/>
		public ReadOnlyMemory<byte> Data { get; private set; }

		/// <inheritdoc/>
		public int Length
		{
			get { return Data.Length; }
		}

		/// <inheritdoc/>
		public IReadOnlyList<int> LineOffsets { get; private set; }

		/// <inheritdoc/>
		public int LineCount
		{
			get { return LineOffsets.Count - 1; }
		}

		/// <inheritdoc/>
		public int AllocatedSize
		{
			get { return Data.Length + (LineOffsets.Count * sizeof(int)); }
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data">Data to construct from</param>
		public ReadOnlyLogText(ReadOnlyMemory<byte> Data)
		{
			ReadOnlySpan<byte> Span = Data.Span;

			int LineCount = LogTextHelpers.GetLineCount(Span, 0);

			int[] LineOffsets = new int[LineCount + 1];
			LogTextHelpers.FindLineOffsets(Span, 0, LineOffsets, 0);
			Debug.Assert(LineOffsets[LineCount] == Data.Length);

			this.Data = Data;
			this.LineOffsets = LineOffsets;
		}
	}

	/// <summary>
	/// Mutable log text object
	/// </summary>
	public class LogText : ILogText
	{
		/// <summary>
		/// Accessor for Data
		/// </summary>
		byte[] InternalData;

		/// <inheritdoc/>
		public ReadOnlyMemory<byte> Data => InternalData.AsMemory(0, Length);

		/// <summary>
		/// Hacky accessor for InternalData, for serializing to Redis
		/// </summary>
		public byte[] InternalDataHACK => InternalData;

		/// <inheritdoc/>
		public int Length
		{
			get;
			private set;
		}

		/// <summary>
		/// Offsets of the start of each line within the data
		/// </summary>
		List<int> InternalLineOffsets = new List<int> { 0 };

		/// <inheritdoc/>
		public IReadOnlyList<int> LineOffsets => InternalLineOffsets;

		/// <inheritdoc/>
		public int LineCount => InternalLineOffsets.Count - 1;

		/// <summary>
		/// Chunk Size
		/// </summary>
		public int MaxLength
		{
			get { return InternalData.Length; }
		}

		/// <summary>
		/// Computes the size of data used by this object
		/// </summary>
		public int AllocatedSize
		{
			get { return InternalData.Length + (InternalLineOffsets.Capacity * sizeof(int)); }
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public LogText()
		{
			this.InternalData = Array.Empty<byte>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data">Data to initialize this chunk with. Ownership of this array is transferred to the chunk, and its length determines the chunk size.</param>
		/// <param name="Length">Number of valid bytes within the initial data array</param>
		public LogText(byte[] Data, int Length)
		{
			this.InternalData = Data;
			this.Length = 0; // Updated below

			UpdateLength(Length);
		}

		/// <summary>
		/// Create a new chunk data object with the given data appended. The internal buffers are reused, with the assumption that
		/// there is no contention over writing to the same location in the chunk.
		/// </summary>
		/// <param name="TextData">The data to append</param>
		/// <returns>New chunk data object</returns>
		public void Append(ReadOnlySpan<byte> TextData)
		{
			TextData.CopyTo(InternalData.AsSpan(Length, TextData.Length));
			UpdateLength(Length + TextData.Length);
		}

		/// <summary>
		/// Updates the plain text representation of this chunk
		/// </summary>
		public void AppendPlainText(ILogText SrcText, int SrcLineIndex, int SrcLineCount)
		{
			// Convert all the lines to plain text
			for (int Idx = 0; Idx < SrcLineCount; Idx++)
			{
				int LineOffset = SrcText.LineOffsets[SrcLineIndex + Idx];
				int NextLineOffset = SrcText.LineOffsets[SrcLineIndex + Idx + 1];
				ReadOnlySpan<byte> InputLine = SrcText.Data.Slice(LineOffset, NextLineOffset - LineOffset).Span;

				// Make sure the output buffer is large enough
				int RequiredSpace = NextLineOffset - LineOffset;
				if (RequiredSpace > InternalData.Length - Length)
				{
					int MaxLineOffset = SrcText.LineOffsets[SrcLineIndex + SrcLineCount];
					Array.Resize(ref InternalData, InternalData.Length + RequiredSpace + (MaxLineOffset - NextLineOffset) / 2);
				}

				// Convert the line to plain text
				Length = ConvertToPlainText(InputLine, InternalData, Length);
				InternalLineOffsets.Add(Length);
			}
		}

		/// <summary>
		/// Determines if the given line is empty
		/// </summary>
		/// <param name="Input">The input data</param>
		/// <returns>True if the given text is empty</returns>
		public static bool IsEmptyOrWhitespace(ReadOnlySpan<byte> Input)
		{
			for(int Idx = 0; Idx < Input.Length; Idx++)
			{
				byte Byte = Input[Idx];
				if(Byte != (byte)'\n' && Byte != '\r' && Byte != ' ')
				{
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Converts a JSON log line to plain text
		/// </summary>
		/// <param name="Input">The JSON data</param>
		/// <param name="Output">Output buffer for the converted line</param>
		/// <param name="OutputOffset">Offset within the buffer to write the converted data</param>
		/// <returns></returns>
		public static int ConvertToPlainText(ReadOnlySpan<byte> Input, byte[] Output, int OutputOffset)
		{
			if(IsEmptyOrWhitespace(Input))
			{
				Output[OutputOffset] = (byte)'\n';
				return OutputOffset + 1;
			}

			Utf8JsonReader Reader = new Utf8JsonReader(Input);
			if (Reader.Read() && Reader.TokenType == JsonTokenType.StartObject)
			{
				while (Reader.Read() && Reader.TokenType == JsonTokenType.PropertyName)
				{
					if (!Reader.ValueTextEquals("message"))
					{
						Reader.Skip();
						continue;
					}
					if (!Reader.Read() || Reader.TokenType != JsonTokenType.String)
					{
						Reader.Skip();
						continue;
					}

					int UnescapedLength = UnescapeUtf8(Reader.ValueSpan, Output.AsSpan(OutputOffset));
					OutputOffset += UnescapedLength;

					Output[OutputOffset] = (byte)'\n';
					OutputOffset++;

					break;
				}
			}
			return OutputOffset;
		}

		/// <summary>
		/// Unescape a json utf8 string
		/// </summary>
		/// <param name="Source">Source span of bytes</param>
		/// <param name="Target">Target span of bytes</param>
		/// <returns>Length of the converted data</returns>
		static int UnescapeUtf8(ReadOnlySpan<byte> Source, Span<byte> Target)
		{
			int Length = 0;
			for (; ; )
			{
				// Copy up to the next backslash
				int Backslash = Source.IndexOf((byte)'\\');
				if (Backslash == -1)
				{
					Source.CopyTo(Target);
					Length += Source.Length;
					break;
				}
				else if (Backslash > 0)
				{
					Source.Slice(0, Backslash).CopyTo(Target);
					Source = Source.Slice(Backslash);
					Target = Target.Slice(Backslash);
					Length += Backslash;
				}

				// Check what the escape code is
				if (Source[1] == 'u')
				{
					char[] Chars = { (char)((StringUtils.ParseHexByte(Source, 2) << 8) | StringUtils.ParseHexByte(Source, 4)) };
					int EncodedLength = Encoding.UTF8.GetBytes(Chars.AsSpan(), Target);
					Source = Source.Slice(6);
					Target = Target.Slice(EncodedLength);
					Length += EncodedLength;
				}
				else
				{
					Target[0] = Source[1] switch
					{
						(byte)'\"' => (byte)'\"',
						(byte)'\\' => (byte)'\\',
						(byte)'\b' => (byte)'\b',
						(byte)'\f' => (byte)'\f',
						(byte)'\n' => (byte)'\n',
						(byte)'\r' => (byte)'\r',
						(byte)'\t' => (byte)'\t',
						_ => Source[1]
					};
					Source = Source.Slice(2);
					Target = Target.Slice(1);
					Length++;
				}
			}
			return Length;
		}

		/// <summary>
		/// Generates placeholder data for a missing span
		/// </summary>
		/// <param name="ChunkIdx">Index of the chunk</param>
		/// <param name="HostName">Host name of the machine that was holding the data</param>
		/// <param name="TargetLength">Desired length of the buffer</param>
		/// <param name="TargetLineCount">Desired line count for the buffer</param>
		public void AppendMissingDataInfo(int ChunkIdx, string? HostName, int TargetLength, int TargetLineCount)
		{
			int NewLength = Length;

			if(InternalData.Length < TargetLength)
			{
				Array.Resize(ref InternalData, TargetLength);
			}

			string Suffix = $" (server: {HostName})";
			for(int NewLineCount = LineCount; NewLineCount < TargetLineCount; NewLineCount++)
			{
				byte[] ErrorBytes = Encoding.ASCII.GetBytes($"{{ \"level\":\"Error\",\"message\":\"[Missing data at chunk {ChunkIdx}, line {NewLineCount}{Suffix}]\" }}\n");
				if (NewLength + ErrorBytes.Length > TargetLength)
				{
					break;
				}

				ErrorBytes.AsSpan().CopyTo(InternalData.AsSpan(NewLength));
				NewLength += ErrorBytes.Length;

				Suffix = String.Empty;
			}

			if (NewLength < TargetLength)
			{
				NewLength = Math.Max(NewLength - 1, 0);
				InternalData.AsSpan(NewLength, (int)((TargetLength - 1) - NewLength)).Fill((byte)' ');
				InternalData[TargetLength - 1] = (byte)'\n';
			}

			UpdateLength((int)TargetLength);
		}

		/// <summary>
		/// Shrinks the data allocated by this chunk to the minimum required
		/// </summary>
		public void Shrink()
		{
			if (Length < InternalData.Length)
			{
				Array.Resize(ref InternalData, Length);
			}
		}

		/// <summary>
		/// Updates the length of this chunk, computing all the newline offsets
		/// </summary>
		/// <param name="NewLength">New length of the chunk</param>
		private void UpdateLength(int NewLength)
		{
			if (NewLength > Length)
			{
				// Make sure the data ends with a newline
				if (InternalData[NewLength - 1] != '\n')
				{
					throw new InvalidDataException("Chunk data must end with a newline");
				}

				// Calculate the new number of newlines
				ReadOnlySpan<byte> NewData = InternalData.AsSpan(0, NewLength);
				for(int Idx = Length; Idx < NewLength; Idx++)
				{
					if(NewData[Idx] == '\n')
					{
						InternalLineOffsets.Add(Idx + 1);
					}
				}

				// Update the size of the buffer
				Length = NewLength;
			}
		}
	}

	/// <summary>
	/// Extension methods for ILogText
	/// </summary>
	public static class LogTextExtensions
	{
		/// <summary>
		/// Find the line index for a particular offset
		/// </summary>
		/// <param name="LogText">The text to search</param>
		/// <param name="Offset">Offset within the text</param>
		/// <returns>The line index</returns>
		public static int GetLineIndexForOffset(this ILogText LogText, int Offset)
		{
			int LineIdx = LogText.LineOffsets.BinarySearch(Offset);
			if(LineIdx < 0)
			{
				LineIdx = ~LineIdx - 1;
			}
			return LineIdx;
		}

		/// <summary>
		/// Converts a log text instance to plain text
		/// </summary>
		/// <param name="LogText">The text to convert</param>
		/// <returns>The plain text instance</returns>
		public static ILogText ToPlainText(this ILogText LogText)
		{
			LogText Other = new LogText();
			Other.AppendPlainText(LogText, 0, LogText.LineCount);
			return Other;
		}
	}
}
