// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;

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
		public ReadOnlySpan<byte> Span
		{
			get { return Memory.Span.Slice(Offset); }
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">The memory to read from</param>
		public MemoryReader(ReadOnlyMemory<byte> Memory)
		{
			this.Memory = Memory;
			this.Offset = 0;
		}

		/// <summary>
		/// Checks that we've used the exact buffer length
		/// </summary>
		/// <param name="ExpectedOffset">Expected offset within the output buffer</param>
		public void CheckOffset(int ExpectedOffset)
		{
			if (Offset != ExpectedOffset)
			{
				throw new Exception($"Serialization is not at expected offset within the input buffer (expected {ExpectedOffset}, actual {Offset})");
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
			sbyte Value = (sbyte)Memory.Span[Offset];
			Offset++;
			return Value;
		}

		/// <summary>
		/// Reads a byte from the buffer
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public byte ReadUInt8()
		{
			byte Value = Memory.Span[Offset];
			Offset++;
			return Value;
		}

		/// <summary>
		/// Reads an int16
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public short ReadInt16()
		{
			short Value = BinaryPrimitives.ReadInt16LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(short);
			return Value;
		}

		/// <summary>
		/// Reads a uint16
		/// </summary>
		/// <returns>The value read from the buffer</returns>
		public ushort ReadUInt16()
		{
			ushort Value = BinaryPrimitives.ReadUInt16LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(ushort);
			return Value;
		}

		/// <summary>
		/// Reads an int32
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public int ReadInt32()
		{
			int Value = BinaryPrimitives.ReadInt32LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(int);
			return Value;
		}

		/// <summary>
		/// Reads a uint32
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public uint ReadUInt32()
		{
			uint Value = BinaryPrimitives.ReadUInt32LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(uint);
			return Value;
		}

		/// <summary>
		/// Reads an int64
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public long ReadInt64()
		{
			long Value = BinaryPrimitives.ReadInt64LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(long);
			return Value;
		}

		/// <summary>
		/// Reads an int64
		/// </summary>
		/// <returns>The value that was read from the buffer</returns>
		public ulong ReadUInt64()
		{
			ulong Value = BinaryPrimitives.ReadUInt64LittleEndian(Memory.Span.Slice(Offset));
			Offset += sizeof(ulong);
			return Value;
		}

		/// <summary>
		/// Reads a sequence of bytes from the buffer
		/// </summary>
		/// <returns>Sequence of bytes</returns>
		public ReadOnlyMemory<byte> ReadVariableLengthBytes()
		{
			int Length = ReadInt32();
			return ReadFixedLengthBytes(Length);
		}

		/// <summary>
		/// Reads a sequence of bytes from the buffer
		/// </summary>
		/// <param name="Length">Number of bytes to read</param>
		/// <returns>Sequence of bytes</returns>
		public ReadOnlyMemory<byte> ReadFixedLengthBytes(int Length)
		{
			ReadOnlyMemory<byte> Bytes = Memory.Slice(Offset, Length);
			Offset += Length;
			return Bytes;
		}

		/// <summary>
		/// Reads a variable length array
		/// </summary>
		/// <param name="ReadItem">Delegate to write an individual item</param>
		public T[] ReadVariableLengthArray<T>(Func<T> ReadItem)
		{
			int Length = ReadInt32();
			return ReadFixedLengthArray(Length, ReadItem);
		}

		/// <summary>
		/// Writes a fixed length array
		/// </summary>
		/// <param name="Length">Length of the array to read</param>
		/// <param name="ReadItem">Delegate to read an individual item</param>
		public T[] ReadFixedLengthArray<T>(int Length, Func<T> ReadItem)
		{
			T[] Array = new T[Length];
			for (int Idx = 0; Idx < Length; Idx++)
			{
				Array[Idx] = ReadItem();
			}
			return Array;
		}
	}
}
