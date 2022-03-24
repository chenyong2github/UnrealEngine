// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;

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
		readonly Memory<byte> _memory;

		/// <summary>
		/// Current offset within the memory
		/// </summary>
		int _offset;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="memory">Memory to write to</param>
		public MemoryWriter(Memory<byte> memory)
		{
			_memory = memory;
			_offset = 0;
		}

		/// <summary>
		/// Checks that we've used the exact buffer length
		/// </summary>
		/// <param name="expectedOffset">Expected offset within the output buffer</param>
		public void CheckOffset(int expectedOffset)
		{
			if (_offset != expectedOffset)
			{
				throw new Exception("Serialization is not at expected offset within the output buffer");
			}
		}

		/// <summary>
		/// Writes a boolean to memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteBoolean(bool value)
		{
			WriteUInt8(value ? (byte)1 : (byte)0);
		}

		/// <summary>
		/// Writes a byte to memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteInt8(sbyte value)
		{
			_memory.Span[_offset] = (byte)value;
			_offset++;
		}

		/// <summary>
		/// Writes a byte to memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteUInt8(byte value)
		{
			_memory.Span[_offset] = value;
			_offset++;
		}

		/// <summary>
		/// Writes an int16 to the memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteInt16(short value)
		{
			BinaryPrimitives.WriteInt16LittleEndian(_memory.Span.Slice(_offset), value);
			_offset += sizeof(short);
		}

		/// <summary>
		/// Writes a uint16 to the memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteUInt16(ushort value)
		{
			BinaryPrimitives.WriteUInt16LittleEndian(_memory.Span.Slice(_offset), value);
			_offset += sizeof(ushort);
		}

		/// <summary>
		/// Writes an int32 to the memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteInt32(int value)
		{
			BinaryPrimitives.WriteInt32LittleEndian(_memory.Span.Slice(_offset), value);
			_offset += sizeof(int);
		}

		/// <summary>
		/// Writes a uint32 to the memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteUInt32(uint value)
		{
			BinaryPrimitives.WriteUInt32LittleEndian(_memory.Span.Slice(_offset), value);
			_offset += sizeof(uint);
		}

		/// <summary>
		/// Writes an int64 to the memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteInt64(long value)
		{
			BinaryPrimitives.WriteInt64LittleEndian(_memory.Span.Slice(_offset), value);
			_offset += sizeof(long);
		}

		/// <summary>
		/// Writes a uint64 to the memory
		/// </summary>
		/// <param name="value">Value to write</param>
		public void WriteUInt64(ulong value)
		{
			BinaryPrimitives.WriteUInt64LittleEndian(_memory.Span.Slice(_offset), value);
			_offset += sizeof(ulong);
		}

		/// <summary>
		/// Writes a variable length span of bytes
		/// </summary>
		/// <param name="bytes">The bytes to write</param>
		public void WriteVariableLengthBytes(ReadOnlySpan<byte> bytes)
		{
			WriteInt32(bytes.Length);
			WriteFixedLengthBytes(bytes);
		}

		/// <summary>
		/// Write a fixed-length sequence of bytes to the buffer
		/// </summary>
		/// <param name="bytes">The bytes to write</param>
		public void WriteFixedLengthBytes(ReadOnlySpan<byte> bytes)
		{
			bytes.CopyTo(_memory.Span.Slice(_offset));
			_offset += bytes.Length;
		}

		/// <summary>
		/// Writes a variable length array
		/// </summary>
		/// <param name="array">The array to write</param>
		/// <param name="writeItem">Delegate to write an individual item</param>
		public void WriteVariableLengthArray<T>(T[] array, Action<T> writeItem)
		{
			WriteInt32(array.Length);
			WriteFixedLengthArray(array, writeItem);
		}

		/// <summary>
		/// Writes a fixed length array
		/// </summary>
		/// <param name="array">The array to write</param>
		/// <param name="writeItem">Delegate to write an individual item</param>
		public void WriteFixedLengthArray<T>(T[] array, Action<T> writeItem)
		{
			for (int idx = 0; idx < array.Length; idx++)
			{
				writeItem(array[idx]);
			}
		}

		/// <summary>
		/// Allocate a writable span from the buffer
		/// </summary>
		/// <param name="length">Length of the span to allocate</param>
		/// <returns>Span that can be written to</returns>
		public Span<byte> AllocateSpan(int length)
		{
			Span<byte> span = _memory.Span.Slice(_offset, length);
			_offset += length;
			return span;
		}
	}
}
