// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace EpicGames.Core
{

	/// <summary>
	/// Comparer for String objects.  However, it implements UE style ignore case compare
	/// </summary>
	public class StringComparerUE : IComparer<string>, IEqualityComparer<string>
	{
		/// <summary>
		/// Static instance of an ordinal String comparer
		/// </summary>
		public static StringComparerUE Ordinal = new StringComparerUE(StringComparison.Ordinal);

		/// <summary>
		/// Static instance of an ordinal String comparer which ignores case
		/// </summary>
		public static StringComparerUE OrdinalIgnoreCase = new StringComparerUE(StringComparison.OrdinalIgnoreCase);

		/// <summary>
		/// The comparison type
		/// </summary>
		public StringComparison ComparisonType { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ComparisonType">Type of comparison to perform</param>
		public StringComparerUE(StringComparison ComparisonType)
		{
			this.ComparisonType = ComparisonType;
		}

		/// <inheritdoc/>
		public bool Equals(string? X, string? Y)
		{
			return string.Equals(X, Y, ComparisonType);
		}

		/// <inheritdoc/>
		public int GetHashCode(string obj)
		{
			return string.GetHashCode(obj, ComparisonType);
		}

		/// <inheritdoc/>
		public int Compare(string? X, string? Y)
		{
			if (ComparisonType == StringComparison.OrdinalIgnoreCase)
			{
				if (X == null)
				{
					return Y == null ? 0 : -1;
				}
				else if (Y == null)
				{
					return 1;
				}

				return StringUtils.CompareIgnoreCaseUE(X.AsSpan(), Y.AsSpan());
			}
			else
			{
				return string.Compare(X, Y, ComparisonType);
			}
		}
	}
}
