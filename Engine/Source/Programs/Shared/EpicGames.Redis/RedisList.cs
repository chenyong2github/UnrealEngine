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
	public readonly struct RedisList<TElement>
	{
		/// <summary>
		/// The database containing this object
		/// </summary>
		IDatabaseAsync Database { get; }

		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RedisList(IDatabaseAsync database, RedisKey key)
		{
			Database = database;
			Key = key;
		}

		/// <inheritdoc cref="IDatabaseAsync.ListGetByIndexAsync(RedisKey, Int64, CommandFlags)"/>
		public async Task<TElement> GetByIndexAsync(long index, CommandFlags flags = CommandFlags.None)
		{
			RedisValue value = await Database.ListGetByIndexAsync(Key, index, flags);
			return value.IsNull? default! : RedisSerializer.Deserialize<TElement>(value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListInsertAfterAsync(RedisKey, RedisValue, RedisValue, CommandFlags)"/>
		public Task<long> InsertAfterAsync(TElement pivot, TElement item, CommandFlags flags = CommandFlags.None)
		{
			RedisValue pivotValue = RedisSerializer.Serialize(pivot);
			RedisValue itemValue = RedisSerializer.Serialize(item);
			return Database.ListInsertAfterAsync(Key, pivotValue, itemValue, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListInsertBeforeAsync(RedisKey, RedisValue, RedisValue, CommandFlags)"/>
		public Task<long> InsertBeforeAsync(TElement pivot, TElement item, CommandFlags flags = CommandFlags.None)
		{
			RedisValue pivotValue = RedisSerializer.Serialize(pivot);
			RedisValue itemValue = RedisSerializer.Serialize(item);
			return Database.ListInsertBeforeAsync(Key, pivotValue, itemValue, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPopAsync(RedisKey, CommandFlags)"/>
		public async Task<TElement> LeftPopAsync(CommandFlags flags = CommandFlags.None)
		{
			RedisValue value = await Database.ListLeftPopAsync(Key, flags);
			return value.IsNull ? default! : RedisSerializer.Deserialize<TElement>(value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPushAsync(RedisKey, RedisValue, When, CommandFlags)"/>
		public Task<long> LeftPushAsync(TElement item, When when = When.Always, CommandFlags flags = CommandFlags.None)
		{
			return Database.ListLeftPushAsync(Key, RedisSerializer.Serialize(item), when, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPushAsync(RedisKey, RedisValue[], When, CommandFlags)"/>
		public Task<long> LeftPushAsync(IEnumerable<TElement> items, When when = When.Always, CommandFlags flags = CommandFlags.None)
		{
			RedisValue[] values = items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.ListLeftPushAsync(Key, values, when, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLengthAsync(RedisKey, CommandFlags)"/>
		public Task<long> LengthAsync()
		{
			return Database.ListLengthAsync(Key);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRangeAsync(RedisKey, Int64, Int64, CommandFlags)"/>
		public async Task<TElement[]> RangeAsync(long start = 0, long stop = -1, CommandFlags flags = CommandFlags.None)
		{
			RedisValue[] values = await Database.ListRangeAsync(Key, start, stop, flags);
			return Array.ConvertAll(values, (Converter<RedisValue, TElement>)(x => (TElement)RedisSerializer.Deserialize<TElement>(x)));
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRemoveAsync(RedisKey, RedisValue, Int64, CommandFlags)"/>
		public Task<long> RemoveAsync(TElement item, long count = 0L, CommandFlags flags = CommandFlags.None)
		{
			RedisValue value = RedisSerializer.Serialize(item);
			return Database.ListRemoveAsync(Key, value, count, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPopAsync(RedisKey, CommandFlags)"/>
		public async Task<TElement> RightPopAsync(CommandFlags flags = CommandFlags.None)
		{
			RedisValue value = await Database.ListRightPopAsync(Key, flags);
			return value.IsNull? default! : RedisSerializer.Deserialize<TElement>(value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPushAsync(RedisKey, RedisValue, When, CommandFlags)"/>
		public Task<long> RightPushAsync(TElement item, When when = When.Always, CommandFlags flags = CommandFlags.None)
		{
			return Database.ListRightPushAsync(Key, RedisSerializer.Serialize(item), when, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPushAsync(RedisKey, RedisValue[], When, CommandFlags)"/>
		public Task<long> RightPushAsync(IEnumerable<TElement> items, When when = When.Always, CommandFlags flags = CommandFlags.None)
		{
			RedisValue[] values = items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.ListRightPushAsync(Key, values, when, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListSetByIndexAsync(RedisKey, Int64, RedisValue, CommandFlags)"/>
		public Task SetByIndexAsync(long index, TElement item, CommandFlags flags = CommandFlags.None)
		{
			RedisValue value = RedisSerializer.Serialize(item);
			return Database.ListSetByIndexAsync(Key, index, value, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListTrimAsync(RedisKey, Int64, Int64, CommandFlags)"/>
		public Task TrimAsync(long start, long stop, CommandFlags flags = CommandFlags.None)
		{
			return Database.ListTrimAsync(Key, start, stop, flags);
		}
	}

	/// <summary>
	/// Extension methods for sets
	/// </summary>
	public static class RedisListExtensions
	{
		/// <summary>
		/// Creates a version of this list which modifies a transaction rather than the direct DB
		/// </summary>
		public static RedisList<TElement> With<TElement>(this ITransaction transaction, RedisList<TElement> set)
		{
			return new RedisList<TElement>(transaction, set.Key);
		}
	}
}
