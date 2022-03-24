// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;

namespace EpicGames.Core
{
	/// <summary>
	/// Reads data from a memory buffer
	/// </summary>
	public class MemoryReader
	{
		/// <summary>
		/// The memory to read from
		/// </summary>
		public ReadOnlyMemory<byte> Memory
		{
			get;
		}

		/// <summary>
		/// Offset to read from
		/// </summary>
		public int Offset
		{
			get; set;
		}

		/// <summary>
		/// Length of the data
		/// </summary>
		public int Length => Memory.Length;

		/// <summary>
		/// Returns the memory at the current offset
		/// </summary>
		public ReadOnlySpan<byte> Span => Memory.Span.Slice(Offset);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="memory">The memory to read from</param>
		public MemoryReader(ReadOnlyMemory<byte> memory)
		{
			Memory = memory;
			Offset = 0;
		}

		/// <summary>
		/// Checks that we've used the exact buffer length
		/// </summary>
		/// <param name="expectedOffset">Expected offset within the output buffer</param>
		public void CheckOffset(int expectedOffset)
		{
			if (Offset != expectedOffset)
			{
				throw new Exception($"Serialization is not at expected offset within the input buffer (expected {expectedOffset}, actual {Offset})");
			}
		}

		/// <summary>
		/// Read a boolean from the buffer
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public bool ReadBoolean()
		{
			return ReadUInt8() != 0;
		}

		/// <summary>
		/// Reads a byte from the buffer
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public sbyte ReadInt8()
		{
			sbyte value = (sbyte)Memory.Span[Offset];
			Offset++;
			return value;
		}

		/// <summary>
		/// Reads a byte from the buffer
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public byte ReadUInt8()
		{
			byte value = Memory.Span[Offset];
			Offset++;
			return value;
		}

		/// <summary>
		/// Reads an int16
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public short ReadInt16()
		{
			short value = BinaryPrimitives.ReadInt16LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(short);
			return value;
		}

		/// <summary>
		/// Reads a uint16
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public ushort ReadUInt16()
		{
			ushort value = BinaryPrimitives.ReadUInt16LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(ushort);
			return value;
		}

		/// <summary>
		/// Reads an int32
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public int ReadInt32()
		{
			int value = BinaryPrimitives.ReadInt32LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(int);
			return value;
		}

		/// <summary>
		/// Reads a uint32
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public uint ReadUInt32()
		{
			uint value = BinaryPrimitives.ReadUInt32LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(uint);
			return value;
		}

		/// <summary>
		/// Reads an int64
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public long ReadInt64()
		{
			long value = BinaryPrimitives.ReadInt64LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(long);
			return value;
		}

		/// <summary>
		/// Reads an int64
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public ulong ReadUInt64()
		{
			ulong value = BinaryPrimitives.ReadUInt64LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(ulong);
			return value;
		}

		/// <summary>
		/// Reads a sequence of bytes from the buffer
		/// </summary>
		/// <returns>Sequence of bytes</returns>
		public ReadOnlyMemory<byte> ReadVariableLengthBytes()
		{
			int length = ReadInt32();
			return ReadFixedLengthBytes(length);
		}

		/// <summary>
		/// Reads a sequence of bytes from the buffer
		/// </summary>
		/// <param name="length">Number of bytes to read</param>
		/// <returns>Sequence of bytes</returns>
		public ReadOnlyMemory<byte> ReadFixedLengthBytes(int length)
		{
			ReadOnlyMemory<byte> bytes = Memory.Slice(Offset, length);
			Offset += length;
			return bytes;
		}

		/// <summary>
		/// Reads a variable length array
		/// </summary>
		/// <param name="readItem">Delegate to write an individual item</param>
		public T[] ReadVariableLengthArray<T>(Func<T> readItem)
		{
			int length = ReadInt32();
			return ReadFixedLengthArray(length, readItem);
		}

		/// <summary>
		/// Writes a fixed length array
		/// </summary>
		/// <param name="length">Length of the array to read</param>
		/// <param name="readItem">Delegate to read an individual item</param>
		public T[] ReadFixedLengthArray<T>(int length, Func<T> readItem)
		{
			T[] array = new T[length];
			for (int idx = 0; idx < length; idx++)
			{
				array[idx] = readItem();
			}
			return array;
		}
	}
}
