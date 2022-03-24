// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;

namespace EpicGames.Core
{
	/// <summary>
	/// Delimits a section of a list
	/// </summary>
	/// <typeparam name="T">The list element type</typeparam>
	public readonly struct ListSegment<T> : IEnumerable<T>, IEnumerable, IReadOnlyCollection<T>, IReadOnlyList<T>
	{
		/// <summary>
		/// Gets the original array containing the range of elements that the list segment
		/// delimits.
		/// </summary>
		public IList<T> List { get; }

		/// <summary>
		/// Gets the position of the first element in the range delimited by the list segment,
		/// relative to the start of the original list.
		/// </summary>
		public int Offset { get; }

		/// <summary>
		/// Gets the number of elements in the range delimited by the array segment.
		/// </summary>
		public int Count { get; }

		/// <summary>
		/// An empty list segment of the given element type
		/// </summary>
		public static ListSegment<T> Empty { get; } = new ListSegment<T>(null!, 0, 0);

		/// <inheritdoc/>
		public bool IsReadOnly => List.IsReadOnly;

		/// <summary>
		/// Initializes a new instance of the System.ArraySegment`1 structure that delimits
		/// all the elements in the specified array.
		/// </summary>
		public ListSegment(IList<T> list)
			: this(list, 0, list.Count)
		{
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="list"></param>
		/// <param name="offset"></param>
		/// <param name="count"></param>
		public ListSegment(IList<T> list, int offset, int count)
		{
			if (offset < 0 || offset > list.Count)
			{
				throw new ArgumentOutOfRangeException(nameof(offset));
			}
			if (count < 0 || offset + count > list.Count)
			{
				throw new ArgumentOutOfRangeException(nameof(count));
			}

			List = list;
			Offset = offset;
			Count = count;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="index"></param>
		/// <returns></returns>
		/// <exception cref="ArgumentOutOfRangeException">Index is not a valid index in the ListSegment</exception>
		public T this[int index]
		{
			get
			{
				if (index < 0 || index >= Count)
				{
					throw new ArgumentOutOfRangeException(nameof(index));
				}
				return List[index + Offset];
			}
			set
			{
				if (index < 0 || index >= Count)
				{
					throw new ArgumentOutOfRangeException(nameof(index));
				}
				List[index + Offset] = value;
			}
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="list"></param>
		public static implicit operator ListSegment<T>(List<T> list)
		{
			return new ListSegment<T>(list);
		}

		/// <summary>
		/// Enumerator implementation
		/// </summary>
		struct Enumerator : IEnumerator<T>, IEnumerator
		{
			readonly IList<T> _list;
			int _index;
			readonly int _maxIndex;

			/// <inheritdoc/>
			public T Current
			{
				get
				{
					if (_index >= _maxIndex)
					{
						throw new IndexOutOfRangeException();
					}
					return _list[_index];
				}
			}

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="segment"></param>
			public Enumerator(ListSegment<T> segment)
			{
				_list = segment.List;
				_index = segment.Offset - 1;
				_maxIndex = segment.Offset + segment.Count;
			}

			/// <inheritdoc/>
			void IDisposable.Dispose()
			{
			}

			/// <inheritdoc/>
			public bool MoveNext()
			{
				return ++_index < _maxIndex;
			}

			/// <inheritdoc/>
			object? IEnumerator.Current => Current;

			/// <inheritdoc/>
			void IEnumerator.Reset() => throw new InvalidOperationException();
		}

		/// <inheritdoc/>
		public IEnumerator<T> GetEnumerator() => new Enumerator(this);

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	/// <summary>
	/// Extension method to slice a list
	/// </summary>
	public static class ListSegmentExtensions
	{
		/// <summary>
		/// Create a list segment
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="list"></param>
		/// <param name="offset"></param>
		/// <returns></returns>
		public static ListSegment<T> Slice<T>(this IList<T> list, int offset)
		{
			return new ListSegment<T>(list, offset, list.Count - offset);
		}

		/// <summary>
		/// Create a list segment
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="list"></param>
		/// <param name="offset"></param>
		/// <param name="count"></param>
		/// <returns></returns>
		public static ListSegment<T> Slice<T>(this IList<T> list, int offset, int count)
		{
			return new ListSegment<T>(list, offset, count);
		}
	}
}
