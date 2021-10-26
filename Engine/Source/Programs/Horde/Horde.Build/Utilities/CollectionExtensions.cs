// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Http.Features;
using Microsoft.AspNetCore.Mvc.Formatters;
using Microsoft.IdentityModel.Tokens;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Extension methods for collections
	/// </summary>
	public static class CollectionExtensions
	{
		/// <summary>
		/// Adds an arbitrary sequence of items to a protobuf map field
		/// </summary>
		/// <typeparam name="TKey">The key type</typeparam>
		/// <typeparam name="TValue">The value type</typeparam>
		/// <param name="Map">The map to update</param>
		/// <param name="Sequence">Sequence of items to add</param>
		public static void Add<TKey, TValue>(this Google.Protobuf.Collections.MapField<TKey, TValue> Map, IEnumerable<KeyValuePair<TKey, TValue>> Sequence)
		{
			foreach(KeyValuePair<TKey, TValue> Pair in Sequence)
			{
				Map.Add(Pair.Key, Pair.Value);
			}
		}
	}
}
