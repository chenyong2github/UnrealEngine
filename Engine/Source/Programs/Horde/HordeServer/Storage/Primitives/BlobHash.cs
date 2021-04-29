// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Security.Cryptography;
using System.Threading.Tasks;

namespace HordeServer.Storage.Primitives
{
	/// <summary>
	/// Stores a reference to an immutable SHA256 hash value. Supports comparison and equality tests by value.
	/// </summary>
	public readonly struct BlobHash : IEquatable<BlobHash>, IComparable<BlobHash>
	{
		/// <summary>
		/// Zero vlaue hash
		/// </summary>
		public static BlobHash Zero { get; } = new BlobHash(new byte[NumBytes]);

		/// <summary>
		/// Length of the hash in bytes
		/// </summary>
		public const int NumBytes = 32;

		/// <summary>
		/// Length of the hash in bits.
		/// </summary>
		public const int NumBits = NumBytes * 8;

		/// <summary>
		/// The memory backing this hash value
		/// </summary>
		public ReadOnlyMemory<byte> Memory { get; }

		/// <summary>
		/// Span for the memory
		/// </summary>
		public ReadOnlySpan<byte> Span => Memory.Span;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Backing storage for the hash. Must be <see cref="NumBytes"/> long.</param>
		public BlobHash(ReadOnlyMemory<byte> Memory)
		{
			if (Memory.Length != NumBytes)
			{
				throw new ArgumentException("Data is incorrect length");
			}
			this.Memory = Memory;
		}

		/// <summary>
		/// Check if one hash starts with the same bits as another
		/// </summary>
		/// <param name="Other">The hash to test against</param>
		/// <param name="Length">Number of bits to compare</param>
		/// <returns>True if the hashes match, false otherwise</returns>
		public bool StartsWith(BlobHash Other, int Length)
		{
			ReadOnlySpan<byte> ThisSpan = Span;
			ReadOnlySpan<byte> OtherSpan = Other.Span;

			int Idx = 0;
			int RemainingLength = Length;

			for (; RemainingLength > 8; RemainingLength -= 8, Idx++)
			{
				if (ThisSpan[Idx] != OtherSpan[Idx])
				{
					return false;
				}
			}

			int Mask = ~((1 << (8 - RemainingLength)) - 1);
			return (ThisSpan[Idx] & Mask) == (OtherSpan[Idx] & Mask);
		}

		/// <summary>
		/// Mask off the left number of bits from the hash
		/// </summary>
		/// <param name="NumBits">Number of bits to include</param>
		/// <returns>Masked hash value</returns>
		public BlobHash MaskLeft(int NumBits)
		{
			ReadOnlySpan<byte> OldData = Span;
			byte[] NewData = new byte[NumBytes];

			int Idx = 0;
			int RemainingBits = NumBits;
			for (; RemainingBits > 8; RemainingBits -= 8, Idx++)
			{
				NewData[Idx] = OldData[Idx];
			}

			NewData[Idx] = (byte)(OldData[Idx] & ~((1 << (8 - RemainingBits)) - 1));
			return new BlobHash(NewData);
		}

		/// <summary>
		/// Compare two hashes for equality
		/// </summary>
		/// <param name="A">First hash</param>
		/// <param name="B">Second hash</param>
		/// <returns>True if the hashes are equal</returns>
		public static bool operator ==(BlobHash A, BlobHash B)
		{
			return A.Span.SequenceEqual(B.Span);
		}

		/// <summary>
		/// Compare two hashes for inequality
		/// </summary>
		/// <param name="A">First hash</param>
		/// <param name="B">Second hash</param>
		/// <returns>True if the hashes are not equal</returns>
		public static bool operator !=(BlobHash A, BlobHash B)
		{
			return !(A == B);
		}

		/// <summary>
		/// Tests whether A > B
		/// </summary>
		public static bool operator >(BlobHash A, BlobHash B)
		{
			return A.CompareTo(B) > 0;
		}

		/// <summary>
		/// Tests whether A is less than B
		/// </summary>
		public static bool operator <(BlobHash A, BlobHash B)
		{
			return A.CompareTo(B) < 0;
		}

		/// <summary>
		/// Tests whether A is greater than or equal to B
		/// </summary>
		public static bool operator >=(BlobHash A, BlobHash B)
		{
			return A.CompareTo(B) >= 0;
		}

		/// <summary>
		/// Tests whether A is less than or equal to B
		/// </summary>
		public static bool operator <=(BlobHash A, BlobHash B)
		{
			return A.CompareTo(B) <= 0;
		}

		/// <summary>
		/// Create a copy of this hash value, by duplicating the underlying memory
		/// </summary>
		/// <returns>Duplicated hash value</returns>
		public BlobHash Clone()
		{
			return new BlobHash(Memory.ToArray());
		}

		/// <inheritdoc/>
		public override bool Equals(object? Other)
		{
			if (Other is BlobHash)
			{
				return Equals((BlobHash)Other);
			}
			else
			{
				return false;
			}
		}

		/// <inheritdoc cref="IEquatable{T}.Equals(T)"/>
		public bool Equals(BlobHash Other)
		{
			return this == Other;
		}

		/// <inheritdoc cref="IComparable{T}.CompareTo(T)"/>
		public int CompareTo(BlobHash Other)
		{
			ReadOnlySpan<byte> A = Span;
			ReadOnlySpan<byte> B = Other.Span;

			for (int Idx = 0; Idx < A.Length && Idx < B.Length; Idx++)
			{
				int Compare = A[Idx] - B[Idx];
				if (Compare != 0)
				{
					return Compare;
				}
			}
			return A.Length - B.Length;
		}

		/// <summary>
		/// Parse a hash value from string
		/// </summary>
		/// <param name="Text"></param>
		/// <returns></returns>
		public static BlobHash Parse(string Text)
		{
			return new BlobHash(StringUtils.ParseHexString(Text));
		}

		/// <summary>
		/// Compute a hash from a block of data
		/// </summary>
		/// <param name="Data"></param>
		/// <returns></returns>
		public static BlobHash Compute(ReadOnlyMemory<byte> Data)
		{
			return Compute(Data.ToArray());
		}

		/// <summary>
		/// Compute a hash from a block of data
		/// </summary>
		/// <param name="Data"></param>
		/// <returns></returns>
		public static BlobHash Compute(byte[] Data)
		{
			using (SHA256 Algorithm = SHA256.Create())
			{
				byte[] Result = Algorithm.ComputeHash(Data);
				return new BlobHash(Result);
			}
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			ReadOnlySpan<byte> LocalSpan = Span;

			HashCode HashCode = new HashCode();
			for (int Idx = 0; Idx < NumBytes; Idx++)
			{
				HashCode.Add(LocalSpan[Idx]);
			}
			return HashCode.ToHashCode();
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return StringUtils.FormatHexString(Span);
		}
	}

	/// <summary>
	/// Read-only array of blob hashes
	/// </summary>
	struct ReadOnlyBlobHashArray : IReadOnlyList<BlobHash>
	{
		/// <summary>
		/// Underlying storage for the array
		/// </summary>
		public ReadOnlyMemory<byte> Memory { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Storage for the array</param>
		public ReadOnlyBlobHashArray(ReadOnlyMemory<byte> Memory)
		{
			this.Memory = Memory;
		}

		/// <summary>
		/// Accessor for elements in the array
		/// </summary>
		/// <param name="Index">Index of the element to retrieve</param>
		/// <returns>Hash at the given index</returns>
		public BlobHash this[int Index] => new BlobHash(Memory.Slice(Index * BlobHash.NumBytes, BlobHash.NumBytes));

		/// <inheritdoc/>
		public int Count => Memory.Length / BlobHash.NumBytes;

		/// <inheritdoc/>
		public IEnumerator<BlobHash> GetEnumerator()
		{
			ReadOnlyMemory<byte> Remaining = Memory;
			while (!Remaining.IsEmpty)
			{
				yield return new BlobHash(Remaining.Slice(0, BlobHash.NumBytes));
				Remaining = Remaining.Slice(BlobHash.NumBytes);
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}
}

