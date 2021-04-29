// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Utilities;
using ICSharpCode.SharpZipLib.BZip2;
using Microsoft.AspNetCore.Routing.Constraints;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// Pending data for a sub-chunk
	/// </summary>
	public class LogSubChunkData
	{
		/// <summary>
		/// Type of data stored in this subchunk
		/// </summary>
		public LogType Type { get; }

		/// <summary>
		/// Offset within the file of this sub-chunk
		/// </summary>
		public long Offset { get; }

		/// <summary>
		/// Length of this sub-chunk
		/// </summary>
		public int Length { get; }

		/// <summary>
		/// Index of the first line in this sub-chunk
		/// </summary>
		public int LineIndex { get; }

		/// <summary>
		/// Gets the number of lines in this sub-chunk
		/// </summary>
		public int LineCount { get; }

		/// <summary>
		/// Text data
		/// </summary>
		ILogText? TextInternal;

		/// <summary>
		/// Compressed text data
		/// </summary>
		ReadOnlyMemory<byte> CompressedTextInternal;

		/// <summary>
		/// The index for this sub-chunk
		/// </summary>
		LogIndexData? IndexInternal;

		/// <summary>
		/// The log text
		/// </summary>
		public ILogText InflateText()
		{
			if (TextInternal == null)
			{
				TextInternal = new ReadOnlyLogText(CompressedTextInternal.DecompressBzip2());
			}
			return TextInternal;
		}

		/// <summary>
		/// The compressed log text
		/// </summary>
		public ReadOnlyMemory<byte> DeflateText()
		{
			if(CompressedTextInternal.IsEmpty)
			{
				CompressedTextInternal = TextInternal!.Data.CompressBzip2();
			}
			return CompressedTextInternal;
		}

		/// <summary>
		/// Index for tokens in this chunk
		/// </summary>
		public LogIndexData BuildIndex()
		{
			if(IndexInternal == null)
			{
				ILogText PlainText = InflateText();
				if (Type != LogType.Text)
				{
					PlainText = PlainText.ToPlainText();
				}

				LogIndexBlock[] Blocks = new LogIndexBlock[1];
				Blocks[0] = new LogIndexBlock(LineIndex, LineCount, PlainText, PlainText.Data.CompressBzip2());
				IndexInternal = new LogIndexData(null, 0, Blocks);
			}
			return IndexInternal;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type">Type of data stored in this subchunk</param>
		/// <param name="Offset">Offset within the file of this sub-chunk</param>
		/// <param name="LineIndex">Index of the first line in this sub-chunk</param>
		/// <param name="Text">Text to add</param>
		public LogSubChunkData(LogType Type, long Offset, int LineIndex, ILogText Text)
		{
			this.Type = Type;
			this.Offset = Offset;
			this.Length = Text.Data.Length;
			this.LineIndex = LineIndex;
			this.LineCount = Text.LineCount;
			this.TextInternal = Text;
		}

		/// <summary>
		/// Constructor for raw data
		/// </summary>
		/// <param name="Type">Type of data stored in this subchunk</param>
		/// <param name="Offset">Offset within the file of this sub-chunk</param>
		/// <param name="Length">Length of the uncompressed data</param>
		/// <param name="LineIndex">Index of the first line</param>
		/// <param name="LineCount">Number of lines in the uncompressed text</param>
		/// <param name="CompressedText">Compressed text data</param>
		/// <param name="Index">Index data</param>
		public LogSubChunkData(LogType Type, long Offset, int Length, int LineIndex, int LineCount, ReadOnlyMemory<byte> CompressedText, LogIndexData? Index)
		{
			this.Type = Type;
			this.Offset = Offset;
			this.Length = Length;
			this.LineIndex = LineIndex;
			this.LineCount = LineCount;
			this.CompressedTextInternal = CompressedText;
			this.IndexInternal = Index;
		}

		/// <summary>
		/// Constructs a sub-chunk from a block of memory. Uses slices of the given memory buffer rather than copying the data.
		/// </summary>
		/// <param name="Reader">The reader to read from</param>
		/// <param name="Offset">Offset of the sub-chunk within this file</param>
		/// <param name="LineIndex">Index of the first line in this subchunk</param>
		/// <returns>New subchunk data</returns>
		public static LogSubChunkData Read(MemoryReader Reader, long Offset, int LineIndex)
		{
			int Version = Reader.ReadInt32(); // Version placeholder
			if (Version == 0)
			{
				LogType Type = (LogType)Reader.ReadInt32();
				int Length = Reader.ReadInt32();
				int LineCount = Reader.ReadInt32();
				ReadOnlyMemory<byte> CompressedText = Reader.ReadVariableLengthBytes();
				Reader.ReadVariableLengthBytes();
				return new LogSubChunkData(Type, Offset, Length, LineIndex, LineCount, CompressedText, null);
			}
			else if (Version == 1)
			{
				LogType Type = (LogType)Reader.ReadInt32();
				int Length = Reader.ReadInt32();
				int LineCount = Reader.ReadInt32();
				ReadOnlyMemory<byte> CompressedText = Reader.ReadVariableLengthBytes();
				ReadOnlyMemory<byte> CompressedPlainText = Reader.ReadVariableLengthBytes();
				ReadOnlyTrie Trie = Reader.ReadTrie();
				LogIndexBlock IndexBlock = new LogIndexBlock(LineIndex, LineCount, null, CompressedPlainText);
				LogIndexData Index = new LogIndexData(Trie, 0, new[] { IndexBlock });
				return new LogSubChunkData(Type, Offset, Length, LineIndex, LineCount, CompressedText, Index);
			}
			else
			{
				LogType Type = (LogType)Reader.ReadInt32();
				int Length = Reader.ReadInt32();
				int LineCount = Reader.ReadInt32();
				ReadOnlyMemory<byte> CompressedText = Reader.ReadVariableLengthBytes();
				LogIndexData Index = Reader.ReadLogIndexData();
				Index.SetBaseLineIndex(LineIndex); // Fix for incorrectly saved data
				return new LogSubChunkData(Type, Offset, Length, LineIndex, LineCount, CompressedText, Index);
			}
		}

		/// <summary>
		/// Serializes the sub-chunk to a stream
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		public void Write(MemoryWriter Writer)
		{
			Writer.WriteInt32(2); // Version placeholder

			Writer.WriteInt32((int)Type);
			Writer.WriteInt32(Length);
			Writer.WriteInt32(LineCount);

			Writer.WriteVariableLengthBytes(DeflateText().Span);
			Writer.WriteLogIndexData(BuildIndex());
		}

		/// <summary>
		/// Serializes this object to a byte array
		/// </summary>
		/// <returns>Byte array</returns>
		public byte[] ToByteArray()
		{
			byte[] Data = new byte[GetSerializedSize()];

			MemoryWriter Writer = new MemoryWriter(Data);
			Write(Writer);
			Writer.CheckOffset(Data.Length);

			return Data;
		}

		/// <summary>
		/// Determines the size of serialized data
		/// </summary>
		public int GetSerializedSize()
		{
			return sizeof(int) + sizeof(int) + sizeof(int) + sizeof(int) + (sizeof(int) + DeflateText().Length) + BuildIndex().GetSerializedSize();
		}
	}

	/// <summary>
	/// Pending data for a sub-chunk
	/// </summary>
	public static class LogSubChunkDataExtensions
	{
		/// <summary>
		/// Constructs a sub-chunk from a block of memory. Uses slices of the given memory buffer rather than copying the data.
		/// </summary>
		/// <param name="Reader">The reader to read from</param>
		/// <param name="Offset">Offset of this sub-chunk within the file</param>
		/// <param name="LineIndex">Index of the first line within this chunk</param>
		/// <returns>New subchunk data</returns>
		public static LogSubChunkData ReadLogSubChunkData(this MemoryReader Reader, long Offset, int LineIndex)
		{
			return LogSubChunkData.Read(Reader, Offset, LineIndex);
		}

		/// <summary>
		/// Serializes the sub-chunk to a stream
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		/// <param name="SubChunkData">The sub-chunk data to write</param>
		public static void WriteLogSubChunkData(this MemoryWriter Writer, LogSubChunkData SubChunkData)
		{
			SubChunkData.Write(Writer);
		}
	}
}
