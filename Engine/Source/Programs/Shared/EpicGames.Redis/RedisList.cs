// Copyright Epic Games, Inc. All Rights Reserved.

using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
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
		public RedisList(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.ListGetByIndexAsync(RedisKey, long, CommandFlags)"/>
		public async Task<TElement> GetByIndexAsync(long Index, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.ListGetByIndexAsync(Key, Index, Flags);
			return Value.IsNull? default(TElement)! : RedisSerializer.Deserialize<TElement>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListInsertAfterAsync(RedisKey, RedisValue, RedisValue, CommandFlags)"/>
		public Task<long> InsertAfterAsync(TElement Pivot, TElement Item, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue PivotValue = RedisSerializer.Serialize(Pivot);
			RedisValue ItemValue = RedisSerializer.Serialize(Item);
			return Database.ListInsertAfterAsync(Key, PivotValue, ItemValue, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListInsertBeforeAsync(RedisKey, RedisValue, RedisValue, CommandFlags)"/>
		public Task<long> InsertBeforeAsync(TElement Pivot, TElement Item, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue PivotValue = RedisSerializer.Serialize(Pivot);
			RedisValue ItemValue = RedisSerializer.Serialize(Item);
			return Database.ListInsertBeforeAsync(Key, PivotValue, ItemValue, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPopAsync(RedisKey, CommandFlags)"/>
		public async Task<TElement> LeftPopAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.ListLeftPopAsync(Key, Flags);
			return Value.IsNull ? default(TElement)! : RedisSerializer.Deserialize<TElement>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPushAsync(RedisKey, RedisValue, When, CommandFlags)"/>
		public Task<long> LeftPushAsync(TElement Item, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.ListLeftPushAsync(Key, RedisSerializer.Serialize(Item), When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPushAsync(RedisKey, RedisValue[], When, CommandFlags)"/>
		public Task<long> LeftPushAsync(IEnumerable<TElement> Items, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = Items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.ListLeftPushAsync(Key, Values, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLengthAsync(RedisKey, CommandFlags)"/>
		public Task<long> LengthAsync()
		{
			return Database.ListLengthAsync(Key);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRangeAsync(RedisKey, long, long, CommandFlags)"/>
		public async Task<TElement[]> RangeAsync(long Start = 0, long Stop = -1, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.ListRangeAsync(Key, Start, Stop, Flags);
			return Array.ConvertAll(Values, (Converter<RedisValue, TElement>)(x => (TElement)RedisSerializer.Deserialize<TElement>(x)));
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRemoveAsync(RedisKey, RedisValue, long, CommandFlags)"/>
		public Task<long> RemoveAsync(TElement Item, long Count = 0L, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = RedisSerializer.Serialize(Item);
			return Database.ListRemoveAsync(Key, Value, Count, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPopAsync(RedisKey, CommandFlags)"/>
		public async Task<TElement> RightPopAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.ListRightPopAsync(Key, Flags);
			return Value.IsNull? default(TElement)! : RedisSerializer.Deserialize<TElement>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPushAsync(RedisKey, RedisValue, When, CommandFlags)"/>
		public Task<long> RightPushAsync(TElement Item, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.ListRightPushAsync(Key, RedisSerializer.Serialize(Item), When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPushAsync(RedisKey, RedisValue[], When, CommandFlags)"/>
		public Task<long> RightPushAsync(IEnumerable<TElement> Items, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = Items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.ListRightPushAsync(Key, Values, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListSetByIndexAsync(RedisKey, long, RedisValue, CommandFlags)"/>
		public Task SetByIndexAsync(long Index, TElement Item, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = RedisSerializer.Serialize(Item);
			return Database.ListSetByIndexAsync(Key, Index, Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListTrimAsync(RedisKey, long, long, CommandFlags)"/>
		public Task TrimAsync(long Start, long Stop, CommandFlags Flags = CommandFlags.None)
		{
			return Database.ListTrimAsync(Key, Start, Stop, Flags);
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
		public static RedisList<TElement> With<TElement>(this ITransaction Transaction, RedisList<TElement> Set)
		{
			return new RedisList<TElement>(Transaction, Set.Key);
		}
	}
}
