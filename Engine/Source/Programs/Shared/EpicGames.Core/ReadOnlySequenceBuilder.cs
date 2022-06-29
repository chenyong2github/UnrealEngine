// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Utility class to combine buffers into a <see cref="ReadOnlySequence{T}"/>
	/// </summary>
	/// <typeparam name="T">Element type</typeparam>
	public class ReadOnlySequenceBuilder<T>
	{
		class Segment : ReadOnlySequenceSegment<T>
		{
			public Segment(long runningIndex, ReadOnlyMemory<T> memory)
			{
				RunningIndex = runningIndex;
				Memory = memory;
			}

			public void SetNext(Segment next)
			{
				Next = next;
			}
		}

		readonly List<ReadOnlyMemory<T>> _segments = new List<ReadOnlyMemory<T>>();

		/// <summary>
		/// Append a block of memory to the end of the sequence
		/// </summary>
		/// <param name="memory">Memory to append</param>
		public void Append(ReadOnlyMemory<T> memory)
		{
			if (memory.Length > 0)
			{
				_segments.Add(memory);
			}
		}

		/// <summary>
		/// Append another sequence to the end of this one
		/// </summary>
		/// <param name="sequence">Sequence to append</param>
		public void Append(ReadOnlySequence<T> sequence)
		{
			foreach (ReadOnlyMemory<T> segment in sequence)
			{
				Append(segment);
			}
		}

		/// <summary>
		/// Construct a sequence from the added blocks
		/// </summary>
		/// <returns>Sequence for the added memory blocks</returns>
		public ReadOnlySequence<T> Construct()
		{
			if (_segments.Count == 0)
			{
				return ReadOnlySequence<T>.Empty;
			}

			Segment first = new Segment(0, _segments[0]);
			Segment last = first;

			for (int idx = 1; idx < _segments.Count; idx++)
			{
				Segment next = new Segment(last.RunningIndex + last.Memory.Length, _segments[idx]);
				last.SetNext(next);
				last = next;
			}

			return new ReadOnlySequence<T>(first, 0, last, last.Memory.Length);
		}
	}
}
