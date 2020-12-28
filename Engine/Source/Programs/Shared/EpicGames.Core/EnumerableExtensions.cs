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
	}
}
