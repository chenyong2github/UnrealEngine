// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	struct LogIndex
	{
		const int NumHashes = 3;
		const int NgramLength = 3;
		const int NgramMask = (1 << (NgramLength * 8)) - 1;

		/// <summary>
		/// Enumerates the hash codes for a given block of text
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <returns></returns>
		public static IEnumerable<int> GetHashCodes(ReadOnlyMemory<byte> Text)
		{
			if(Text.Length >= NgramLength)
			{
				int Value = 0;
				for(int Idx = 0; Idx < NgramLength - 1; Idx++)
				{
					Value = (Value << 8) | ToLowerUtf8(Text.Span[Idx]);
				}
				for(int Idx = NgramLength - 1; Idx < Text.Length; Idx++)
				{
					Value = ((Value << 8) | ToLowerUtf8(Text.Span[Idx])) & NgramMask;

					int HashValue = Value;
					yield return HashValue;

					for(int HashIdx = 1; HashIdx < NumHashes; HashIdx++)
					{
						HashValue = Scramble(HashValue);
						yield return HashValue;
					}
				}
			}
		}

		/// <summary>
		/// Converts a character into a format for hashing
		/// </summary>
		/// <param name="Value">The input byte</param>
		/// <returns>The value to include in the trigram</returns>
		static byte ToLowerUtf8(byte Value)
		{
			if (Value >= 'A' && Value <= 'Z')
			{
				return (byte)(Value + 'a' - 'A');
			}
			else
			{
				return Value;
			}
		}

		/// <summary>
		/// Scramble the given number using a pseudo-RNG
		/// </summary>
		/// <param name="Value">Initial value</param>
		/// <returns>The scrambled value</returns>
		private static int Scramble(int Value)
		{
			Value ^= Value << 13;
			Value ^= Value >> 17;
			Value ^= Value << 5;
			return Value;
		}
	}

	/// <summary>
	/// Read only implementation of a bloom filter
	/// </summary>
	public class ReadOnlyBloomFilter
	{
		/// <summary>
		/// Data for the bloom filter
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data">Data to construct from</param>
		public ReadOnlyBloomFilter(ReadOnlyMemory<byte> Data)
		{
			this.Data = Data;
		}

		/// <summary>
		/// Determines if the filter may contain a value
		/// </summary>
		/// <param name="HashCodes">Sequence of hash codes</param>
		/// <returns>True if the value is in the set</returns>
		public bool Contains(IEnumerable<int> HashCodes)
		{
			ReadOnlySpan<byte> Span = Data.Span;
			foreach (int HashCode in HashCodes)
			{
				int Index = HashCode & ((Span.Length << 3) - 1);
				if ((Span[Index >> 3] & (1 << (Index & 7))) == 0)
				{
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Calculates the load of this filter (ie. the proportion of bits which are set)
		/// </summary>
		/// <returns>Value between 0 and 1</returns>
		public double CalculateLoadFactor()
		{
			int SetCount = 0;

			ReadOnlySpan<byte> Span = Data.Span;
			for (int Idx = 0; Idx < Span.Length; Idx++)
			{
				for (int Bit = 0; Bit < 8; Bit++)
				{
					if ((Span[Idx] & (1 << Bit)) != 0)
					{
						SetCount++;
					}
				}
			}

			return (double)SetCount / (Span.Length * 8);
		}
	}

	/// <summary>
	/// Implementation of a bloom filter
	/// </summary>
	public class BloomFilter
	{
		/// <summary>
		/// The array of bits in the filter
		/// </summary>
		public byte[] Data { get; }

		/// <summary>
		/// Constructs a new filter of the given size
		/// </summary>
		/// <param name="MinSize">The minimum size of the filter. Will be rounded up to the next power of two.</param>
		public BloomFilter(int MinSize)
		{
			int Size = MinSize;
			while ((Size & (Size - 1)) != 0)
			{
				Size |= (Size - 1);
				Size++;
			}
			Data = new byte[Size];
		}

		/// <summary>
		/// Constructs a filter from raw data
		/// </summary>
		/// <param name="Data">The data to construct from</param>
		public BloomFilter(byte[] Data)
		{
			this.Data = Data;
			if ((Data.Length & (Data.Length - 1)) != 0)
			{
				throw new ArgumentException("Array for bloom filter must be a power of 2 in size.");
			}
		}

		/// <summary>
		/// Add a value to the filter
		/// </summary>
		/// <param name="HashCodes">Sequence of hash codes</param>
		public void Add(IEnumerable<int> HashCodes)
		{
			foreach(int HashCode in HashCodes)
			{
				int Index = HashCode & ((Data.Length << 3) - 1);
				Data[Index >> 3] = (byte)(Data[Index >> 3] | (1 << (Index & 7)));
			}
		}

		/// <summary>
		/// Determines if the filter may contain a value
		/// </summary>
		/// <param name="HashCodes">Sequence of hash codes</param>
		/// <returns>True if the value is in the set</returns>
		public bool Contains(IEnumerable<int> HashCodes)
		{
			foreach (int HashCode in HashCodes)
			{
				int Index = HashCode & ((Data.Length << 3) - 1);
				if ((Data[Index >> 3] & (1 << (Index & 7))) == 0)
				{
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Calculates the load of this filter (ie. the proportion of bits which are set)
		/// </summary>
		/// <returns>Value between 0 and 1</returns>
		public double CalculateLoadFactor()
		{
			int SetCount = 0;
			int AllCount = 0;
			for (int Idx = 0; Idx < Data.Length; Idx++)
			{
				for (int Bit = 0; Bit < 8; Bit++)
				{
					if ((Data[Idx] & (1 << Bit)) != 0)
					{
						SetCount++;
					}
					AllCount++;
				}
			}
			return (double)SetCount / AllCount;
		}
	}
}
