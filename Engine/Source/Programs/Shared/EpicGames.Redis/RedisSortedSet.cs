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
	public readonly struct RedisSortedSet<TElement>
	{
		readonly IDatabaseAsync Database;

		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Key"></param>
		public RedisSortedSet(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public Task<bool> AddAsync<T>(T Value, double Score, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetAddAsync(Key, RedisSerializer.Serialize(Value), Score, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public Task<long> AddAsync<T>(SortedSetEntry<T>[] Values, CommandFlags Flags = CommandFlags.None)
		{
			SortedSetEntry[] Untyped = Array.ConvertAll(Values, x => new SortedSetEntry(RedisSerializer.Serialize(x.Element), x.Score));
			return Database.SortedSetAddAsync(Key, Untyped, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> RemoveAsync<T>(T Value, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetRemoveAsync(Key, RedisSerializer.Serialize(Value), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveRangeByScoreAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public async Task<long> RemoveRangeByScoreAsync<T>(double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, CommandFlags Flags = CommandFlags.None)
		{
			return await Database.SortedSetRemoveRangeByScoreAsync(Key, Start, Stop, Exclude, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public async IAsyncEnumerable<SortedSetEntry<T>> ScanAsync<T>(RedisValue Pattern = default, int PageSize = 250, long Cursor = 0, int PageOffset = 0, CommandFlags Flags = CommandFlags.None)
		{
			await foreach (SortedSetEntry Entry in Database.SortedSetScanAsync(Key, Pattern, PageSize, Cursor, PageOffset, Flags))
			{
				yield return new SortedSetEntry<T>(Entry);
			}
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByRankAsync(RedisKey, long, long, Order, CommandFlags)"/>
		public async Task<T[]> RangeByRankAsync<T>(long Start, long Stop = -1, Order Order = Order.Ascending, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SortedSetRangeByRankAsync(Key, Start, Stop, Order, Flags);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<T>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByScoreAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public async Task<T[]> RangeByScoreAsync<T>(double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, Order Order = Order.Ascending, long Skip = 0L, long Take = -1L, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SortedSetRangeByScoreAsync(Key, Start, Stop, Exclude, Order, Skip, Take, Flags);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<T>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByScoreWithScoresAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public async Task<SortedSetEntry<T>[]> RangeByScoreWithScoresAsync<T>(double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, Order Order = Order.Ascending, long Skip = 0L, long Take = -1L, CommandFlags Flags = CommandFlags.None)
		{
			SortedSetEntry[] Values = await Database.SortedSetRangeByScoreWithScoresAsync(Key, Start, Stop, Exclude, Order, Skip, Take, Flags);
			return Array.ConvertAll(Values, x => new SortedSetEntry<T>(x));
		}
	}

	/// <summary>
	/// Extension methods for sets
	/// </summary>
	public static class RedisSortedSetExtensions
	{
		/// <summary>
		/// Creates a version of this set which modifies a transaction rather than the direct DB
		/// </summary>
		/// <param name="Transaction"></param>
		/// <returns></returns>
		public static RedisSortedSet<TElement> With<TElement>(this ITransaction Transaction, RedisSortedSet<TElement> Set)
		{
			return new RedisSortedSet<TElement>(Transaction, Set.Key);
		}
	}
}
