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
		/// <param name="Key"></param>
		public RedisList(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLengthAsync(RedisKey, CommandFlags)"/>
		public Task<long> LengthAsync()
		{
			return Database.ListLengthAsync(Key);
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

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPopAsync(RedisKey, CommandFlags)">
		public async Task<TElement> LeftPopAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.ListLeftPopAsync(Key, Flags);
			return Value.IsNull ? default(TElement)! : RedisSerializer.Deserialize<TElement>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRangeAsync(RedisKey, CommandFlags)">
		public async Task<TElement[]> RangeAsync(long Start = 0, long Stop = -1, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.ListRangeAsync(Key, Start, Stop, Flags);
			return Array.ConvertAll(Values, (Converter<RedisValue, TElement>)(x => (TElement)RedisSerializer.Deserialize<TElement>(x)));
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

		/// <inheritdoc cref="IDatabaseAsync.ListTrimAsync(RedisKey, long, long, CommandFlags)">
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
		/// <param name="Transaction"></param>
		/// <returns></returns>
		public static RedisList<TElement> With<TElement>(this ITransaction Transaction, RedisList<TElement> Set)
		{
			return new RedisList<TElement>(Transaction, Set.Key);
		}
	}
}
