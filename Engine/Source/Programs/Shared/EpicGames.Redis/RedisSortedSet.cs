// Copyright Epic Games, Inc. All Rights Reserved.

using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace EpicGames.Redis
{
	/// <summary>
	/// Represents a typed Redis list with a given key
	/// </summary>
	/// <typeparam name="TElement">The type of element stored in the list</typeparam>
	public readonly struct RedisSortedSet<TElement> : IEquatable<RedisSortedSet<TElement>>
	{
		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Key"></param>
		public RedisSortedSet(RedisKey Key) => this.Key = Key;

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is RedisSortedSet<TElement> Set && Key == Set.Key;

		/// <inheritdoc/>
		public bool Equals(RedisSortedSet<TElement> Other) => Key == Other.Key;

		/// <inheritdoc/>
		public override int GetHashCode() => Key.GetHashCode();

		/// <summary>Compares two instances for equality</summary>
		public static bool operator ==(RedisSortedSet<TElement> Left, RedisSortedSet<TElement> Right) => Left.Key == Right.Key;

		/// <summary>Compares two instances for equality</summary>
		public static bool operator !=(RedisSortedSet<TElement> Left, RedisSortedSet<TElement> Right) => Left.Key != Right.Key;
	}

	/// <summary>
	/// Extension methods for <see cref="IDatabaseAsync"/>
	/// </summary>
	public static class RedisSortedSetExtensions
	{
		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public static Task SortedSetAddAsync<T>(this IDatabaseAsync Database, RedisSortedSet<T> Set, T Value, double Score, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetAddAsync(Set.Key, RedisSerializer.Serialize(Value), Score, Flags);
		}
	}
}
