// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Numerics;
using System.Text;

namespace EpicGames.Serialization
{
	/// <summary>
	/// Methods for reading VarUInt values
	/// </summary>
	public static class VarInt
	{
		/// <summary>
		/// Read a variable-length unsigned integer.
		/// </summary>
		/// <param name="Buffer">A variable-length encoding of an unsigned integer</param>
		/// <returns></returns>
		public static ulong Read(ReadOnlySpan<byte> Buffer)
		{
			return Read(Buffer, out _);
		}

		/// <summary>
		/// Read a variable-length unsigned integer.
		/// </summary>
		/// <param name="Buffer">A variable-length encoding of an unsigned integer</param>
		/// <param name="BytesRead">The number of bytes consumed from the input</param>
		/// <returns></returns>
		public static ulong Read(ReadOnlySpan<byte> Buffer, out int BytesRead)
		{
			BytesRead = (int)Measure(Buffer);

			ulong Value = (ulong)(Buffer[0] & (0xff >> BytesRead));
			for (int i = 1; i < BytesRead; i++)
			{
				Value <<= 8;
				Value |= Buffer[i];
			}
			return Value;
		}

		/// <summary>
		/// Measure the length in bytes (1-9) of an encoded variable-length integer.
		/// </summary>
		/// <param name="Buffer">A variable-length encoding of an(signed or unsigned) integer.</param>
		/// <returns>The number of bytes used to encode the integer, in the range 1-9.</returns>
		public static int Measure(ReadOnlySpan<byte> Buffer)
		{
			byte b = Buffer[0];
			b = (byte)~b;
			return BitOperations.LeadingZeroCount(b) - 23;
		}

		/// <summary>
		/// Measure the number of bytes (1-5) required to encode the 32-bit input.
		/// </summary>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static int Measure(int Value)
		{
			return Measure((uint)Value);
		}

		/// <summary>
		/// Measure the number of bytes (1-5) required to encode the 32-bit input.
		/// </summary>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static int Measure(uint Value)
		{
			return BitOperations.Log2(Value) / 7 + 1;
		}

		/// <summary>
		/// Measure the number of bytes (1-9) required to encode the 64-bit input.
		/// </summary>
		/// <param name="value"></param>
		/// <returns></returns>
		public static int Measure(ulong value)
		{
			return Math.Min(BitOperations.Log2(value) / 7 + 1, 9);
		}

		/// <summary>
		/// Write a variable-length unsigned integer.
		/// </summary>
		/// <param name="Value">An unsigned integer to encode</param>
		/// <param name="Buffer">A buffer of at least 9 bytes to write the output to.</param>
		/// <returns>The number of bytes used in the output</returns>
		public static int Write(Span<byte> Buffer, long Value)
		{
			return Write(Buffer, (ulong)Value);
		}

		/// <summary>
		/// Write a variable-length unsigned integer.
		/// </summary>
		/// <param name="Value">An unsigned integer to encode</param>
		/// <param name="Buffer">A buffer of at least 9 bytes to write the output to.</param>
		/// <returns>The number of bytes used in the output</returns>
		public static int Write(Span<byte> Buffer, ulong Value)
		{
			int ByteCount = Measure(Value);

			for (int Idx = 1; Idx < ByteCount; Idx++)
			{
				Buffer[ByteCount - Idx] = (byte)Value;
				Value >>= 8;
			}
			Buffer[0] = (byte)((0xff << (9 - (int)ByteCount)) | (byte)Value);
			return ByteCount;
		}
	}
}
