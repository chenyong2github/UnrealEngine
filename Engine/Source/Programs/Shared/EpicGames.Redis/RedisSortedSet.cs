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
		public RedisSortedSet(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public Task<bool> AddAsync(TElement Item, double Score, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = RedisSerializer.Serialize(Item);
			return Database.SortedSetAddAsync(Key, Value, Score, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, When, CommandFlags)"/>
		public Task<long> AddAsync(SortedSetEntry<TElement>[] Values, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			SortedSetEntry[] Untyped = Array.ConvertAll(Values, x => new SortedSetEntry(RedisSerializer.Serialize(x.Element), x.Score));
			return Database.SortedSetAddAsync(Key, Untyped, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetLengthAsync(RedisKey, double, double, Exclude, CommandFlags)"/>
		public Task<long> LengthAsync(double Min = double.NegativeInfinity, double Max = double.PositiveInfinity, Exclude Exclude = Exclude.None, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetLengthAsync(Key, Min, Max, Exclude, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetLengthByValueAsync(RedisKey, RedisValue, RedisValue, Exclude, CommandFlags)"/>
		public Task<long> LengthByValueAsync(TElement Min, TElement Max, Exclude Exclude = Exclude.None, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue MinValue = RedisSerializer.Serialize(Min);
			RedisValue MaxValue = RedisSerializer.Serialize(Max);
			return Database.SortedSetLengthByValueAsync(Key, MinValue, MaxValue, Exclude, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByRankAsync(RedisKey, long, long, Order, CommandFlags)"/>
		public async Task<TElement[]> RangeByRankAsync(long Start, long Stop = -1, Order Order = Order.Ascending, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SortedSetRangeByRankAsync(Key, Start, Stop, Order, Flags);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<TElement>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByRankWithScoresAsync(RedisKey, long, long, Order, CommandFlags)"/>
		public async Task<SortedSetEntry<TElement>[]> RangeByRankWithScoresAsync(long Start, long Stop = -1, Order Order = Order.Ascending, CommandFlags Flags = CommandFlags.None)
		{
			SortedSetEntry[] Values = await Database.SortedSetRangeByRankWithScoresAsync(Key, Start, Stop, Order, Flags);
			return Array.ConvertAll(Values, x => new SortedSetEntry<TElement>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByScoreAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public async Task<TElement[]> RangeByScoreAsync(double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, Order Order = Order.Ascending, long Skip = 0L, long Take = -1L, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SortedSetRangeByScoreAsync(Key, Start, Stop, Exclude, Order, Skip, Take, Flags);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<TElement>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByScoreWithScoresAsync(RedisKey, double, double, Exclude, Order, long, long, CommandFlags)"/>
		public async Task<SortedSetEntry<TElement>[]> RangeByScoreWithScoresAsync(double Start = double.NegativeInfinity, double Stop = double.PositiveInfinity, Exclude Exclude = Exclude.None, Order Order = Order.Ascending, long Skip = 0L, long Take = -1L, CommandFlags Flags = CommandFlags.None)
		{
			SortedSetEntry[] Values = await Database.SortedSetRangeByScoreWithScoresAsync(Key, Start, Stop, Exclude, Order, Skip, Take, Flags);
			return Array.ConvertAll(Values, x => new SortedSetEntry<TElement>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRangeByValueAsync(RedisKey, RedisValue, RedisValue, Exclude, Order, long, long, CommandFlags)"/>
		public async Task<TElement[]> RangeByValueAsync(TElement Min, TElement Max, Exclude Exclude = Exclude.None, Order Order = Order.Ascending, long Skip = 0L, long Take = -1L, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue MinValue = RedisSerializer.Serialize(Min);
			RedisValue MaxValue = RedisSerializer.Serialize(Max);
			RedisValue[] Values = await Database.SortedSetRangeByValueAsync(Key, MinValue, MaxValue, Exclude, Order, Skip, Take, Flags);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<TElement>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRankAsync(RedisKey, RedisValue, Order, CommandFlags)"/>
		public Task<long?> RankAsync(TElement Item, Order Order = Order.Ascending, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetRankAsync(Key, RedisSerializer.Serialize(Item), Order, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> RemoveAsync(TElement Value, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetRemoveAsync(Key, RedisSerializer.Serialize(Value), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public Task<long> RemoveAsync(IEnumerable<TElement> Items, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = Items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.SortedSetRemoveAsync(Key, Values, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveRangeByRankAsync(RedisKey, long, long, CommandFlags)"/>
		public Task<long> RemoveRangeByRankAsync(long Start, long Stop, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetRemoveRangeByRankAsync(Key, Start, Stop, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveRangeByScoreAsync(RedisKey, double, double, Exclude, CommandFlags)"/>
		public Task<long> RemoveRangeByScoreAsync(double Start, double Stop, Exclude Exclude = Exclude.None, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SortedSetRemoveRangeByScoreAsync(Key, Start, Stop, Exclude, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetRemoveRangeByValueAsync(RedisKey, RedisValue, RedisValue, Exclude, CommandFlags)"/>
		public async Task<long> RemoveRangeByValueAsync(TElement Min, TElement Max, Exclude Exclude = Exclude.None, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue MinValue = RedisSerializer.Serialize(Min);
			RedisValue MaxValue = RedisSerializer.Serialize(Max);
			return await Database.SortedSetRemoveRangeByValueAsync(Key, MinValue, MaxValue, Exclude, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetAddAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public async IAsyncEnumerable<SortedSetEntry<TElement>> ScanAsync(RedisValue Pattern = default, int PageSize = 250, long Cursor = 0, int PageOffset = 0, CommandFlags Flags = CommandFlags.None)
		{
			await foreach (SortedSetEntry Entry in Database.SortedSetScanAsync(Key, Pattern, PageSize, Cursor, PageOffset, Flags))
			{
				yield return new SortedSetEntry<TElement>(Entry);
			}
		}

		/// <inheritdoc cref="IDatabaseAsync.SortedSetScoreAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<double?> ScoreAsync(TElement Member, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = RedisSerializer.Serialize(Member);
			return Database.SortedSetScoreAsync(Key, Value, Flags);
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
		public static RedisSortedSet<TElement> With<TElement>(this ITransaction Transaction, RedisSortedSet<TElement> Set)
		{
			return new RedisSortedSet<TElement>(Transaction, Set.Key);
		}
	}
}
