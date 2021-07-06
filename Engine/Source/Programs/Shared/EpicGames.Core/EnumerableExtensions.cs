// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Extension methods for <see cref="IEnumerable{T}"/>
	/// </summary>
	public static class EnumerableExtensions
	{
		/// <summary>
		/// Split the sequence into batches of at most the given size
		/// </summary>
		/// <typeparam name="TElement">The element type</typeparam>
		/// <param name="Sequence">Sequence to split into batches</param>
		/// <param name="BatchSize">Maximum size of each batch</param>
		/// <returns>Sequence of batches</returns>
		public static IEnumerable<IReadOnlyList<TElement>> Batch<TElement>(this IEnumerable<TElement> Sequence, int BatchSize)
		{
			List<TElement> Elements = new List<TElement>(BatchSize);
			foreach (TElement Element in Sequence)
			{
				Elements.Add(Element);
				if (Elements.Count == BatchSize)
				{
					yield return Elements;
					Elements.Clear();
				}
			}
			if (Elements.Count > 0)
			{
				yield return Elements;
			}
		}

		/// <summary>
		/// Finds the minimum element by a given field
		/// </summary>
		/// <typeparam name="TElement"></typeparam>
		/// <param name="Sequence"></param>
		/// <param name="Selector"></param>
		/// <returns></returns>
		public static TElement MinBy<TElement>(this IEnumerable<TElement> Sequence, Func<TElement, int> Selector)
		{
			IEnumerator<TElement> Enumerator = Sequence.GetEnumerator();
			if (!Enumerator.MoveNext())
			{
				throw new Exception("Collection is empty");
			}

			TElement MinElement = Enumerator.Current;

			int MinValue = Selector(MinElement);
			while (Enumerator.MoveNext())
			{
				int Value = Selector(Enumerator.Current);
				if (Value < MinValue)
				{
					MinElement = Enumerator.Current;
				}
			}

			return MinElement;
		}

		/// <summary>
		/// Finds the maximum element by a given field
		/// </summary>
		/// <typeparam name="TElement"></typeparam>
		/// <param name="Sequence"></param>
		/// <param name="Selector"></param>
		/// <returns></returns>
		public static TElement MaxBy<TElement>(this IEnumerable<TElement> Sequence, Func<TElement, int> Selector)
		{
			IEnumerator<TElement> Enumerator = Sequence.GetEnumerator();
			if (!Enumerator.MoveNext())
			{
				throw new Exception("Collection is empty");
			}

			TElement MaxElement = Enumerator.Current;

			int MaxValue = Selector(MaxElement);
			while (Enumerator.MoveNext())
			{
				int Value = Selector(Enumerator.Current);
				if (Value > MaxValue)
				{
					MaxElement = Enumerator.Current;
				}
			}

			return MaxElement;
		}
	}
}
