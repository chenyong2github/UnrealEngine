// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Extension methods for functionality on <see cref="List{T}"/> but missing from <see cref="IReadOnlyList{T}"/>
	/// </summary>
	public static class ReadOnlyListExtensions
	{
		/// <summary>
		/// Performs a binary search on the given list
		/// </summary>
		/// <typeparam name="T">The element type for the list</typeparam>
		/// <param name="List">The list type</param>
		/// <param name="Item">Item to search for</param>
		/// <returns>As List.BinarySearch</returns>
		public static int BinarySearch<T>(this IReadOnlyList<T> List, T Item)
		{
			return BinarySearch(List, x => x, Item, Comparer<T>.Default);
		}

		/// <summary>
		/// Performs a binary search on the given list
		/// </summary>
		/// <typeparam name="T">The element type for the list</typeparam>
		/// <param name="List">The list type</param>
		/// <param name="Item">Item to search for</param>
		/// <param name="Comparer">Comparer for elements in the list</param>
		/// <returns>As List.BinarySearch</returns>
		public static int BinarySearch<T>(this IReadOnlyList<T> List, T Item, IComparer<T> Comparer)
		{
			return BinarySearch(List, x => x, Item, Comparer);
		}

		/// <summary>
		/// Binary searches a list based on a projection
		/// </summary>
		/// <typeparam name="TItem">The item in the list</typeparam>
		/// <typeparam name="TField">The field to search on</typeparam>
		/// <param name="List">The list to search</param>
		/// <param name="Projection">The projection to apply to each item in the list</param>
		/// <param name="Item">The item to find</param>
		/// <returns>As <see cref="List{T}.BinarySearch(T)"/></returns>
		public static int BinarySearch<TItem, TField>(this IReadOnlyList<TItem> List, Func<TItem, TField> Projection, TField Item)
		{
			return BinarySearch(List, Projection, Item, Comparer<TField>.Default);
		}

		/// <summary>
		/// Binary searches a list based on a projection
		/// </summary>
		/// <typeparam name="TItem">The item in the list</typeparam>
		/// <typeparam name="TField">The field to search on</typeparam>
		/// <param name="List">The list to search</param>
		/// <param name="Projection">The projection to apply to each item in the list</param>
		/// <param name="Item">The item to find</param>
		/// <param name="Comparer">Comparer for field elements</param>
		/// <returns>As <see cref="List{T}.BinarySearch(T)"/></returns>
		public static int BinarySearch<TItem, TField>(this IReadOnlyList<TItem> List, Func<TItem, TField> Projection, TField Item, IComparer<TField> Comparer)
		{
			int LowerBound = 0;
			int UpperBound = List.Count - 1;
			while (LowerBound <= UpperBound)
			{
				int Idx = LowerBound + (UpperBound - LowerBound) / 2;

				int Comparison = Comparer.Compare(Projection(List[Idx]), Item);
				if (Comparison == 0)
				{
					return Idx;
				}
				else if (Comparison < 0)
				{
					LowerBound = Idx + 1;
				}
				else
				{
					UpperBound = Idx - 1;
				}
			}
			return ~LowerBound;
		}

		/// <summary>
		/// Converts a read only list to a different type
		/// </summary>
		/// <typeparam name="TInput">The input element type</typeparam>
		/// <typeparam name="TOutput">The output element type</typeparam>
		/// <param name="Input">Input list</param>
		/// <param name="Convert">Conversion function</param>
		/// <returns>New list of items</returns>
		public static List<TOutput> ConvertAll<TInput, TOutput>(this IReadOnlyList<TInput> Input, Func<TInput, TOutput> Convert)
		{
			List<TOutput> Outputs = new List<TOutput>(Input.Count);
			for (int Idx = 0; Idx < Input.Count; Idx++)
			{
				Outputs.Add(Convert(Input[Idx]));
			}
			return Outputs;
		}

		/// <summary>
		/// Finds the index of the first element matching a predicate
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="List">List to search</param>
		/// <param name="Predicate">Predicate for the list</param>
		/// <returns>Index of the element</returns>
		public static int FindIndex<T>(this IReadOnlyList<T> List, Predicate<T> Predicate)
		{
			int FoundIndex = -1;
			for(int Idx = 0; Idx < List.Count; Idx++)
			{
				if (Predicate(List[Idx]))
				{
					FoundIndex = Idx;
					break;
				}
			}
			return FoundIndex;
		}
	}
}
