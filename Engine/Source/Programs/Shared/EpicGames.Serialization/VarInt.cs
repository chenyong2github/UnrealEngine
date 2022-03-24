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
		/// <param name="buffer">A variable-length encoding of an unsigned integer</param>
		/// <returns></returns>
		public static ulong Read(ReadOnlySpan<byte> buffer)
		{
			return Read(buffer, out _);
		}

		/// <summary>
		/// Read a variable-length unsigned integer.
		/// </summary>
		/// <param name="buffer">A variable-length encoding of an unsigned integer</param>
		/// <param name="bytesRead">The number of bytes consumed from the input</param>
		/// <returns></returns>
		public static ulong Read(ReadOnlySpan<byte> buffer, out int bytesRead)
		{
			bytesRead = (int)Measure(buffer);

			ulong value = (ulong)(buffer[0] & (0xff >> bytesRead));
			for (int i = 1; i < bytesRead; i++)
			{
				value <<= 8;
				value |= buffer[i];
			}
			return value;
		}

		/// <summary>
		/// Measure the length in bytes (1-9) of an encoded variable-length integer.
		/// </summary>
		/// <param name="buffer">A variable-length encoding of an(signed or unsigned) integer.</param>
		/// <returns>The number of bytes used to encode the integer, in the range 1-9.</returns>
		public static int Measure(ReadOnlySpan<byte> buffer)
		{
			byte b = buffer[0];
			b = (byte)~b;
			return BitOperations.LeadingZeroCount(b) - 23;
		}

		/// <summary>
		/// Measure the number of bytes (1-5) required to encode the 32-bit input.
		/// </summary>
		/// <param name="value"></param>
		/// <returns></returns>
		public static int Measure(int value)
		{
			return Measure((ulong)(long)value);
		}

		/// <summary>
		/// Measure the number of bytes (1-5) required to encode the 32-bit input.
		/// </summary>
		/// <param name="value"></param>
		/// <returns></returns>
		public static int Measure(uint value)
		{
			return BitOperations.Log2(value) / 7 + 1;
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
		/// <param name="value">An unsigned integer to encode</param>
		/// <param name="buffer">A buffer of at least 9 bytes to write the output to.</param>
		/// <returns>The number of bytes used in the output</returns>
		public static int Write(Span<byte> buffer, long value)
		{
			return Write(buffer, (ulong)value);
		}

		/// <summary>
		/// Write a variable-length unsigned integer.
		/// </summary>
		/// <param name="value">An unsigned integer to encode</param>
		/// <param name="buffer">A buffer of at least 9 bytes to write the output to.</param>
		/// <returns>The number of bytes used in the output</returns>
		public static int Write(Span<byte> buffer, ulong value)
		{
			int byteCount = Measure(value);

			for (int idx = 1; idx < byteCount; idx++)
			{
				buffer[byteCount - idx] = (byte)value;
				value >>= 8;
			}
			buffer[0] = (byte)((0xff << (9 - (int)byteCount)) | (byte)value);
			return byteCount;
		}
	}
}
