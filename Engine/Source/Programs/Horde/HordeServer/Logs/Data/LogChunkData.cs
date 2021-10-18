// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// Data for a log chunk
	/// </summary>
	public class LogChunkData
	{
		/// <summary>
		/// Offset of this chunk
		/// </summary>
		public long Offset { get; }

		/// <summary>
		/// Length of this chunk
		/// </summary>
		public int Length { get; }

		/// <summary>
		/// Line index of this chunk
		/// </summary>
		public int LineIndex { get; }

		/// <summary>
		/// Number of lines in this chunk
		/// </summary>
		public int LineCount { get; }
			
		/// <summary>
		/// List of sub-chunks
		/// </summary>
		public IReadOnlyList<LogSubChunkData> SubChunks { get; }

		/// <summary>
		/// Offset of each sub-chunk within this chunk
		/// </summary>
		public int[] SubChunkOffset { get; }

		/// <summary>
		/// Total line count of the chunk after each sub-chunk
		/// </summary>
		public int[] SubChunkLineIndex { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Offset">Offset of this chunk</param>
		/// <param name="LineIndex">First line index of this chunk</param>
		/// <param name="SubChunks">Sub-chunks for this chunk</param>
		public LogChunkData(long Offset, int LineIndex, IReadOnlyList<LogSubChunkData> SubChunks)
		{
			this.Offset = Offset;
			this.LineIndex = LineIndex;
			this.SubChunks = SubChunks;
			this.SubChunkOffset = CreateSumLookup(SubChunks, x => x.Length);
			this.SubChunkLineIndex = CreateSumLookup(SubChunks, x => x.LineCount);

			if(SubChunks.Count > 0)
			{
				int LastSubChunkIdx = SubChunks.Count - 1;
				Length = SubChunkOffset[LastSubChunkIdx] + SubChunks[LastSubChunkIdx].Length;
				LineCount = SubChunkLineIndex[LastSubChunkIdx] + SubChunks[LastSubChunkIdx].LineCount;
			}
		}

		/// <summary>
		/// Creates a lookup table by summing a field across a list of subchunks
		/// </summary>
		/// <param name="SubChunks"></param>
		/// <param name="Field"></param>
		/// <returns></returns>
		static int[] CreateSumLookup(IReadOnlyList<LogSubChunkData> SubChunks, Func<LogSubChunkData, int> Field)
		{
			int[] Total = new int[SubChunks.Count];

			int Value = 0;
			for (int Idx = 0; Idx + 1 < SubChunks.Count; Idx++)
			{
				Value += Field(SubChunks[Idx]);
				Total[Idx + 1] = Value;
			}

			return Total;
		}

		/// <summary>
		/// Gets the offset of a line within the file
		/// </summary>
		/// <param name="LineIdx">The line index</param>
		/// <returns>Offset of the line within the file</returns>
		public long GetLineOffsetWithinChunk(int LineIdx)
		{
			int SubChunkIdx = SubChunkLineIndex.BinarySearch(LineIdx);
			if (SubChunkIdx < 0)
			{
				SubChunkIdx = ~SubChunkIdx - 1;
			}
			return SubChunkOffset[SubChunkIdx] + SubChunks[SubChunkIdx].InflateText().LineOffsets[LineIdx - SubChunkLineIndex[SubChunkIdx]];
		}

		/// <summary>
		/// Gets the sub chunk index for the given line
		/// </summary>
		/// <param name="ChunkLineIdx">Line index within the chunk</param>
		/// <returns>Subchunk line index</returns>
		public int GetSubChunkForLine(int ChunkLineIdx)
		{
			int SubChunkIdx = SubChunkLineIndex.BinarySearch(ChunkLineIdx);
			if (SubChunkIdx < 0)
			{
				SubChunkIdx = ~SubChunkIdx - 1;
			}
			return SubChunkIdx;
		}

		/// <summary>
		/// Gets the index of the sub-chunk containing the given offset
		/// </summary>
		/// <param name="Offset">Offset to search for</param>
		/// <returns>Index of the sub-chunk</returns>
		public int GetSubChunkForOffsetWithinChunk(int Offset)
		{
			int SubChunkIdx = SubChunkOffset.BinarySearch(Offset);
			if (SubChunkIdx < 0)
			{
				SubChunkIdx = ~SubChunkIdx - 1;
			}
			return SubChunkIdx;
		}

		/// <summary>
		/// Signature for output data
		/// </summary>
		const int CurrentSignature = 'L' | ('C' << 8) | ('D' << 16);

		/// <summary>
		/// Read a log chunk from the given stream
		/// </summary>
		/// <param name="Reader">The reader to read from</param>
		/// <param name="Offset">Offset of this chunk within the file</param>
		/// <param name="LineIndex">Line index of this chunk</param>
		/// <returns>New log chunk data</returns>
		public static LogChunkData Read(MemoryReader Reader, long Offset, int LineIndex)
		{
			int Signature = Reader.ReadInt32();
			if ((Signature & 0xffffff) != CurrentSignature)
			{
				List<LogSubChunkData> SubChunks = new List<LogSubChunkData>();
				SubChunks.Add(new LogSubChunkData(LogType.Json, Offset, LineIndex, new ReadOnlyLogText(Reader.Memory)));
				Reader.Offset = Reader.Memory.Length;
				return new LogChunkData(Offset, LineIndex, SubChunks);
			}

			int Version = Signature >> 24;
			if (Version == 0)
			{
				List<LogSubChunkData> SubChunks = ReadSubChunkList(Reader, Offset, LineIndex);
				Reader.ReadVariableLengthBytes();
				return new LogChunkData(Offset, LineIndex, SubChunks);
			}
			else
			{
				List<LogSubChunkData> SubChunks = ReadSubChunkList(Reader, Offset, LineIndex);
				return new LogChunkData(Offset, LineIndex, SubChunks);
			}
		}

		/// <summary>
		/// Read a list of sub-chunks from the given stream
		/// </summary>
		/// <param name="Reader">The reader to read from</param>
		/// <param name="SubChunkOffset">Offset of this chunk within the file</param>
		/// <param name="SubChunkLineIndex">Line index of this chunk</param>
		/// <returns>List of sub-chunks</returns>
		static List<LogSubChunkData> ReadSubChunkList(MemoryReader Reader, long SubChunkOffset, int SubChunkLineIndex)
		{
			int NumSubChunks = Reader.ReadInt32();

			List<LogSubChunkData> SubChunks = new List<LogSubChunkData>();
			for (int Idx = 0; Idx < NumSubChunks; Idx++)
			{
				LogSubChunkData SubChunkData = Reader.ReadLogSubChunkData(SubChunkOffset, SubChunkLineIndex);
				SubChunkOffset += SubChunkData.Length;
				SubChunkLineIndex += SubChunkData.LineCount;
				SubChunks.Add(SubChunkData);
			}

			return SubChunks;
		}

		/// <summary>
		/// Construct an object from flat memory buffer
		/// </summary>
		/// <param name="Memory">Memory buffer</param>
		/// <param name="Offset">Offset of this chunk within the file</param>
		/// <param name="LineIndex">Line index of this chunk</param>
		/// <returns>Log chunk data</returns>
		public static LogChunkData FromMemory(ReadOnlyMemory<byte> Memory, long Offset, int LineIndex)
		{
			MemoryReader Reader = new MemoryReader(Memory);
			LogChunkData ChunkData = Read(Reader, Offset, LineIndex);
			Reader.CheckOffset(Memory.Length);
			return ChunkData;
		}

		/// <summary>
		/// Write the chunk data to a stream
		/// </summary>
		/// <returns>Serialized data</returns>
		public void Write(MemoryWriter Writer)
		{
			Writer.WriteInt32(CurrentSignature | (1 << 24));

			Writer.WriteInt32(SubChunks.Count);
			foreach (LogSubChunkData SubChunk in SubChunks)
			{
				Writer.WriteLogSubChunkData(SubChunk);
			}
		}

		/// <summary>
		/// Construct an object from flat memory buffer
		/// </summary>
		/// <returns>Log chunk data</returns>
		public byte[] ToByteArray()
		{
			byte[] Data = new byte[GetSerializedSize()];
			MemoryWriter Writer = new MemoryWriter(Data);
			Write(Writer);
			Writer.CheckOffset(Data.Length);
			return Data;
		}

		/// <summary>
		/// Determines the size of the serialized buffer
		/// </summary>
		public int GetSerializedSize()
		{
			return sizeof(int) + (sizeof(int) + SubChunks.Sum(x => x.GetSerializedSize()));
		}
	}

	/// <summary>
	/// Extensions for the log chunk data
	/// </summary>
	static class LogChunkDataExtensions
	{
		/// <summary>
		/// Read a log chunk from the given stream
		/// </summary>
		/// <param name="Reader">The reader to read from</param>
		/// <param name="Offset">Offset of this chunk within the file</param>
		/// <param name="LineIndex">Line index of this chunk</param>
		/// <returns>New log chunk data</returns>
		public static LogChunkData ReadLogChunkData(this MemoryReader Reader, long Offset, int LineIndex)
		{
			return LogChunkData.Read(Reader, Offset, LineIndex);
		}

		/// <summary>
		/// Write the chunk data to a stream
		/// </summary>
		/// <returns>Serialized data</returns>
		public static void WriteLogChunkData(this MemoryWriter Writer, LogChunkData ChunkData)
		{
			ChunkData.Write(Writer);
		}
	}
}
