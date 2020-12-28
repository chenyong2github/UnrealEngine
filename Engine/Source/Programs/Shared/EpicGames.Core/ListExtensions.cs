// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Extension methods for lists
	/// </summary>s
	public static class ListExtensions
	{
		/// <summary>
		/// Sorts a list by a particular field
		/// </summary>
		/// <typeparam name="TElement">List element</typeparam>
		/// <typeparam name="TField">Field type to sort by</typeparam>
		/// <param name="List">List to sort</param>
		/// <param name="Selector">Selects a field from the element</param>
		public static void SortBy<TElement, TField>(this List<TElement> List, Func<TElement, TField> Selector)
		{
			IComparer<TField> DefaultComparer = Comparer<TField>.Default;
			SortBy(List, Selector, DefaultComparer);
		}

		/// <summary>
		/// Sorts a list by a particular field
		/// </summary>
		/// <typeparam name="TElement">List element</typeparam>
		/// <typeparam name="TField">Field type to sort by</typeparam>
		/// <param name="List">List to sort</param>
		/// <param name="Selector">Selects a field from the element</param>
		/// <param name="Comparer">Comparer for fields</param>
		public static void SortBy<TElement, TField>(this List<TElement> List, Func<TElement, TField> Selector, IComparer<TField> Comparer)
		{
			List.Sort((X, Y) => Comparer.Compare(Selector(X), Selector(Y)));
		}
	}
}
