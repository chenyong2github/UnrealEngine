// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace EpicGames.Core
{
	/// <summary>
	/// Extension methods for lists
	/// </summary>s
	public static class ListExtensions
	{
		/// <summary>
		/// Performs a binary search on the given list
		/// </summary>
		/// <typeparam name="T">The element type for the list</typeparam>
		/// <param name="list">The list type</param>
		/// <param name="item">Item to search for</param>
		/// <returns>As List.BinarySearch</returns>
		public static int BinarySearch<T>(this IList<T> list, T item)
		{
			return BinarySearch(list, x => x, item, Comparer<T>.Default);
		}

		/// <summary>
		/// Performs a binary search on the given list
		/// </summary>
		/// <typeparam name="T">The element type for the list</typeparam>
		/// <param name="list">The list type</param>
		/// <param name="item">Item to search for</param>
		/// <param name="comparer">Comparer for elements in the list</param>
		/// <returns>As List.BinarySearch</returns>
		public static int BinarySearch<T>(this IList<T> list, T item, IComparer<T> comparer)
		{
			return BinarySearch(list, x => x, item, comparer);
		}

		/// <summary>
		/// Binary searches a list based on a projection
		/// </summary>
		/// <typeparam name="TItem">The item in the list</typeparam>
		/// <typeparam name="TField">The field to search on</typeparam>
		/// <param name="list">The list to search</param>
		/// <param name="projection">The projection to apply to each item in the list</param>
		/// <param name="item">The item to find</param>
		/// <returns>As <see cref="List{T}.BinarySearch(T)"/></returns>
		public static int BinarySearch<TItem, TField>(this IList<TItem> list, Func<TItem, TField> projection, TField item)
		{
			return BinarySearch(list, projection, item, Comparer<TField>.Default);
		}

		/// <summary>
		/// Binary searches a list based on a projection
		/// </summary>
		/// <typeparam name="TItem">The item in the list</typeparam>
		/// <typeparam name="TField">The field to search on</typeparam>
		/// <param name="list">The list to search</param>
		/// <param name="projection">The projection to apply to each item in the list</param>
		/// <param name="item">The item to find</param>
		/// <param name="comparer">Comparer for field elements</param>
		/// <returns>As <see cref="List{T}.BinarySearch(T)"/></returns>
		public static int BinarySearch<TItem, TField>(this IList<TItem> list, Func<TItem, TField> projection, TField item, IComparer<TField> comparer)
		{
			int lowerBound = 0;
			int upperBound = list.Count - 1;
			while (lowerBound <= upperBound)
			{
				int idx = lowerBound + (upperBound - lowerBound) / 2;

				int comparison = comparer.Compare(projection(list[idx]), item);
				if (comparison == 0)
				{
					return idx;
				}
				else if (comparison < 0)
				{
					lowerBound = idx + 1;
				}
				else
				{
					upperBound = idx - 1;
				}
			}
			return ~lowerBound;
		}

		/// <summary>
		/// Sorts a list by a particular field
		/// </summary>
		/// <typeparam name="TElement">List element</typeparam>
		/// <typeparam name="TField">Field type to sort by</typeparam>
		/// <param name="list">List to sort</param>
		/// <param name="selector">Selects a field from the element</param>
		public static void SortBy<TElement, TField>(this List<TElement> list, Func<TElement, TField> selector)
		{
			IComparer<TField> defaultComparer = Comparer<TField>.Default;
			SortBy(list, selector, defaultComparer);
		}

		/// <summary>
		/// Sorts a list by a particular field
		/// </summary>
		/// <typeparam name="TElement">List element</typeparam>
		/// <typeparam name="TField">Field type to sort by</typeparam>
		/// <param name="list">List to sort</param>
		/// <param name="selector">Selects a field from the element</param>
		/// <param name="comparer">Comparer for fields</param>
		public static void SortBy<TElement, TField>(this List<TElement> list, Func<TElement, TField> selector, IComparer<TField> comparer)
		{
			list.Sort((x, y) => comparer.Compare(selector(x), selector(y)));
		}
	}
}
