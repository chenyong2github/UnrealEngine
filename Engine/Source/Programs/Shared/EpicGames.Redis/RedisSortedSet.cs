// Copyright Epic Games, Inc. All Rights Reserved.

using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace EpicGames.Redis
{
	/// <summary>
	/// Typed implementation of <see cref="SortedSetEntry"/>
	/// </summary>
	/// <typeparam name="T">The element type</typeparam>
	public readonly struct SortedSetEntry<T>
	{
		/// <summary>
		/// Accessor for the element type
		/// </summary>
		public readonly T Element { get; }

		/// <summary>
		/// Score for the entry
		/// </summary>
		public readonly double Score;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Entry"></param>
		public SortedSetEntry(SortedSetEntry Entry)
		{
			this.Element = RedisSerializer.Deserialize<T>(Entry.Element);
			this.Score = Entry.Score;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Element"></param>
		/// <param name="Score"></param>
		public SortedSetEntry(T Element, double Score)
		{
			this.Element = Element;
			this.Score = Score;
		}
	}

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
		public static Task<bool> SortedSetAddAsync<T>(this IDatabaseAsync Database, RedisSortedSet<T> Set, T Value, double Score, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetAddAsync(Set.Key, RedisSerializer.Serialize(Value), Score, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public static Task<long> SortedSetAddAsync<T>(this IDatabaseAsync Database, RedisSortedSet<T> Set, SortedSetEntry<T>[] Values, CommandFlags Flags = CommandFlags.None)
		{
			SortedSetEntry[] Untyped = Array.ConvertAll(Values, x => new SortedSetEntry(RedisSerializer.Serialize(x.Element), x.Score));
			return Database.SortedSetAddAsync(Set.Key, Untyped, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveRangeByScoreAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public static async Task<long> SortedSetRemoveRangeByScoreAsync<T>(this IDatabaseAsync Database, RedisSortedSet<T> Set, double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, CommandFlags Flags = CommandFlags.None)
		{
			return await Database.SortedSetRemoveRangeByScoreAsync(Set.Key, Start, Stop, Exclude, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public static async IAsyncEnumerable<SortedSetEntry<T>> SortedSetScanAsync<T>(this IDatabaseAsync Database, RedisSortedSet<T> Set, RedisValue Pattern = default, int PageSize = 250, long Cursor = 0, int PageOffset = 0, CommandFlags Flags = CommandFlags.None)
		{
			await foreach (SortedSetEntry Entry in Database.SortedSetScanAsync(Set.Key, Pattern, PageSize, Cursor, PageOffset, Flags))
			{
				yield return new SortedSetEntry<T>(Entry);
			}
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByScoreAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public static async Task<T[]> SortedSetRangeByScoreAsync<T>(this IDatabaseAsync Database, RedisSortedSet<T> Set, double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, Order Order = Order.Ascending, long Skip = 0L, long Take = -1L, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SortedSetRangeByScoreAsync(Set.Key, Start, Stop, Exclude, Order, Skip, Take, Flags);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<T>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByScoreWithScoresAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public static async Task<SortedSetEntry<T>[]> SortedSetRangeByScoreWithScoresAsync<T>(this IDatabaseAsync Database, RedisSortedSet<T> Set, double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, Order Order = Order.Ascending, long Skip = 0L, long Take = -1L, CommandFlags Flags = CommandFlags.None)
		{
			SortedSetEntry[] Values = await Database.SortedSetRangeByScoreWithScoresAsync(Set.Key, Start, Stop, Exclude, Order, Skip, Take, Flags);
			return Array.ConvertAll(Values, x => new SortedSetEntry<T>(x));
		}

	}
}
