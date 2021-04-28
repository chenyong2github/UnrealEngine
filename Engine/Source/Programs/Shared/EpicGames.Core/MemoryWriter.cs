// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Writes into a fixed size memory block
	/// </summary>
	public class MemoryWriter
	{
		/// <summary>
		/// The memory block to write to
		/// </summary>
		Memory<byte> Memory;

		/// <summary>
		/// Current offset within the memory
		/// </summary>
		int Offset;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Memory to write to</param>
		public MemoryWriter(Memory<byte> Memory)
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
				throw new Exception("Serialization is not at expected offset within the output buffer");
			}
		}

		/// <summary>
		/// Writes a boolean to memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteBoolean(bool Value)
		{
			WriteUInt8(Value ? (byte)1 : (byte)0);
		}

		/// <summary>
		/// Writes a byte to memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteInt8(sbyte Value)
		{
			Memory.Span[Offset] = (byte)Value;
			Offset++;
		}

		/// <summary>
		/// Writes a byte to memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteUInt8(byte Value)
		{
			Memory.Span[Offset] = Value;
			Offset++;
		}

		/// <summary>
		/// Writes an int16 to the memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteInt16(short Value)
		{
			BinaryPrimitives.WriteInt16LittleEndian(Memory.Span.Slice(Offset), Value);
			Offset += sizeof(short);
		}

		/// <summary>
		/// Writes a uint16 to the memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteUInt16(ushort Value)
		{
			BinaryPrimitives.WriteUInt16LittleEndian(Memory.Span.Slice(Offset), Value);
			Offset += sizeof(ushort);
		}

		/// <summary>
		/// Writes an int32 to the memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteInt32(int Value)
		{
			BinaryPrimitives.WriteInt32LittleEndian(Memory.Span.Slice(Offset), Value);
			Offset += sizeof(int);
		}

		/// <summary>
		/// Writes a uint32 to the memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteUInt32(uint Value)
		{
			BinaryPrimitives.WriteUInt32LittleEndian(Memory.Span.Slice(Offset), Value);
			Offset += sizeof(uint);
		}

		/// <summary>
		/// Writes an int64 to the memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteInt64(long Value)
		{
			BinaryPrimitives.WriteInt64LittleEndian(Memory.Span.Slice(Offset), Value);
			Offset += sizeof(long);
		}

		/// <summary>
		/// Writes a uint64 to the memory
		/// </summary>
		/// <param name="Value">Value to write</param>
		public void WriteUInt64(ulong Value)
		{
			BinaryPrimitives.WriteUInt64LittleEndian(Memory.Span.Slice(Offset), Value);
			Offset += sizeof(ulong);
		}

		/// <summary>
		/// Writes a variable length span of bytes
		/// </summary>
		/// <param name="Bytes">The bytes to write</param>
		public void WriteVariableLengthBytes(ReadOnlySpan<byte> Bytes)
		{
			WriteInt32(Bytes.Length);
			WriteFixedLengthBytes(Bytes);
		}

		/// <summary>
		/// Write a fixed-length sequence of bytes to the buffer
		/// </summary>
		/// <param name="Bytes">The bytes to write</param>
		public void WriteFixedLengthBytes(ReadOnlySpan<byte> Bytes)
		{
			Bytes.CopyTo(Memory.Span.Slice(Offset));
			Offset += Bytes.Length;
		}

		/// <summary>
		/// Writes a variable length array
		/// </summary>
		/// <param name="Array">The array to write</param>
		/// <param name="WriteItem">Delegate to write an individual item</param>
		public void WriteVariableLengthArray<T>(T[] Array, Action<T> WriteItem)
		{
			WriteInt32(Array.Length);
			WriteFixedLengthArray(Array, WriteItem);
		}

		/// <summary>
		/// Writes a fixed length array
		/// </summary>
		/// <param name="Array">The array to write</param>
		/// <param name="WriteItem">Delegate to write an individual item</param>
		public void WriteFixedLengthArray<T>(T[] Array, Action<T> WriteItem)
		{
			for (int Idx = 0; Idx < Array.Length; Idx++)
			{
				WriteItem(Array[Idx]);
			}
		}
	}
}
