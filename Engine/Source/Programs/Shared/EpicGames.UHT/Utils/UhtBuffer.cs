// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Numerics;
using System.Text;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Cached character buffer system.
	/// 
	/// Invoke UhtBuffer.Borrow method to get a buffer of the given size.
	/// Invoke UhtBuffer.Return to return the buffer to the cache.
	/// </summary>
	public class UhtBuffer
	{
		/// <summary>
		/// Any requests of the given size or smaller will be placed in bucket zero with the given size.
		/// </summary>
		private static int MinSize = 1024 * 16;

		/// <summary>
		/// Adjustment to the bucket index to account for the minimum bucket size
		/// </summary>
		private static int BuckedAdjustment = BitOperations.Log2((uint)UhtBuffer.MinSize);
		
		/// <summary>
		/// Total number of supported buckets
		/// </summary>
		private static int BucketCount = 32 - UhtBuffer.BuckedAdjustment;

		/// <summary>
		/// Bucket lookaside list
		/// </summary>
		private static UhtBuffer?[] LookAsideArray = new UhtBuffer?[UhtBuffer.BucketCount];

		/// <summary>
		/// The bucket index associated with the buffer
		/// </summary>
		private int Bucket;

		/// <summary>
		/// Single list link to the next cached buffer
		/// </summary>
		private UhtBuffer? NextBuffer = null;

		/// <summary>
		/// The backing character block.  The size of the array will normally be larger than the 
		/// requested size.
		/// </summary>
		public char[] Block;

		/// <summary>
		/// Memory region sized to the requested size
		/// </summary>
		public Memory<char> Memory;

		/// <summary>
		/// Construct a new buffer
		/// </summary>
		/// <param name="Size">The initial size of the buffer</param>
		/// <param name="Bucket">The bucket associated with the buffer</param>
		/// <param name="BucketSize">The size all blocks in this bucket</param>
		private UhtBuffer(int Size, int Bucket, int BucketSize)
		{
			this.Block = new char[BucketSize];
			this.Bucket = Bucket;
			Reset(Size);
		}

		/// <summary>
		/// Reset the memory region to the given size
		/// </summary>
		/// <param name="Size"></param>
		public void Reset(int Size)
		{
			this.Memory = new Memory<char>(this.Block, 0, Size);
		}

		/// <summary>
		/// Borrow a new buffer of the given size
		/// </summary>
		/// <param name="Size">Size of the buffer</param>
		/// <returns>Buffer that should be returned with a call to Return</returns>
		public static UhtBuffer Borrow(int Size)
		{
			if (Size <= UhtBuffer.MinSize)
			{
				return BorrowInternal(Size, 0, UhtBuffer.MinSize);
			}
			else
			{

				// Round up the size to the next larger power of two if it isn't a power of two
				uint USize = (uint)Size;
				--USize;
				USize |= USize >> 1;
				USize |= USize >> 2;
				USize |= USize >> 4;
				USize |= USize >> 8;
				USize |= USize >> 16;
				++USize;
				int Bucket = BitOperations.Log2(USize) - UhtBuffer.BuckedAdjustment;
				return BorrowInternal(Size, Bucket, (int)USize);
			}
		}

		/// <summary>
		/// Return a buffer initialized with the string builder.
		/// </summary>
		/// <param name="Builder">Source builder content</param>
		/// <returns>Buffer that should be returned with a call to Return</returns>
		public static UhtBuffer Borrow(StringBuilder Builder)
		{
			int Length = Builder.Length;
			UhtBuffer Buffer = Borrow(Length);
			Builder.CopyTo(0, Buffer.Memory.Span, Length);
			return Buffer;
		}

		/// <summary>
		/// Return a buffer initialized with the string builder sub string.
		/// </summary>
		/// <param name="Builder">Source builder content</param>
		/// <param name="StartIndex">Starting index in the builder</param>
		/// <param name="Length">Length of the content</param>
		/// <returns>Buffer that should be returned with a call to Return</returns>
		public static UhtBuffer Borrow(StringBuilder Builder, int StartIndex, int Length)
		{
			UhtBuffer Buffer = Borrow(Length);
			Builder.CopyTo(StartIndex, Buffer.Memory.Span, Length);
			return Buffer;
		}

		/// <summary>
		/// Return the buffer to the cache.  The buffer should no longer be accessed.
		/// </summary>
		/// <param name="Buffer">The buffer to be returned.</param>
		public static void Return(UhtBuffer Buffer)
		{
			lock (UhtBuffer.LookAsideArray)
			{
				Buffer.NextBuffer = UhtBuffer.LookAsideArray[Buffer.Bucket];
				UhtBuffer.LookAsideArray[Buffer.Bucket] = Buffer;
			}
		}

		/// <summary>
		/// Internal helper to allocate a buffer
		/// </summary>
		/// <param name="Size">The initial size of the buffer</param>
		/// <param name="Bucket">The bucket associated with the buffer</param>
		/// <param name="BucketSize">The size all blocks in this bucket</param>
		/// <returns>The allocated buffer</returns>
		private static UhtBuffer BorrowInternal(int Size, int Bucket, int BucketSize)
		{
			lock (UhtBuffer.LookAsideArray)
			{
				if (UhtBuffer.LookAsideArray[Bucket] != null)
				{
					UhtBuffer Buffer = UhtBuffer.LookAsideArray[Bucket]!;
					UhtBuffer.LookAsideArray[Bucket] = Buffer.NextBuffer;
					Buffer.Reset(Size);
					return Buffer;
				}
			}
			return new UhtBuffer(Size, Bucket, BucketSize);
		}
	}

	/// <summary>
	/// Helper class for using pattern to borrow and return a buffer.
	/// </summary>
	public struct UhtBorrowBuffer : IDisposable
	{

		/// <summary>
		/// The borrowed buffer
		/// </summary>
		public UhtBuffer Buffer;

		/// <summary>
		/// Borrow a buffer with the given size
		/// </summary>
		/// <param name="Size">The size to borrow</param>
		public UhtBorrowBuffer(int Size)
		{
			this.Buffer = UhtBuffer.Borrow(Size);
		}

		/// <summary>
		/// Borrow a buffer populated with the builder contents
		/// </summary>
		/// <param name="Builder">Initial contents of the buffer</param>
		public UhtBorrowBuffer(StringBuilder Builder)
		{
			this.Buffer = UhtBuffer.Borrow(Builder);
		}

		/// <summary>
		/// Borrow a buffer populated with the builder contents
		/// </summary>
		/// <param name="Builder">Initial contents of the buffer</param>
		/// <param name="StartIndex">Starting index into the builder</param>
		/// <param name="Length">Length of the data in the builder</param>
		public UhtBorrowBuffer(StringBuilder Builder, int StartIndex, int Length)
		{
			this.Buffer = UhtBuffer.Borrow(Builder, StartIndex, Length);
		}

		/// <summary>
		/// Return the borrowed buffer to the cache
		/// </summary>
		public void Dispose()
		{
			UhtBuffer.Return(this.Buffer);
		}
	}
}
