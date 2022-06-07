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
	public readonly struct RedisSet<TElement>
	{
		readonly IDatabaseAsync _database;

		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RedisSet(IDatabaseAsync database, RedisKey key)
		{
			_database = database;
			Key = key;
		}

		/// <inheritdoc cref="IDatabaseAsync.SetAddAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> AddAsync(TElement item, CommandFlags flags = CommandFlags.None)
		{
			return _database.SetAddAsync(Key, RedisSerializer.Serialize(item), flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetAddAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public Task<long> AddAsync(IEnumerable<TElement> items, CommandFlags flags = CommandFlags.None)
		{
			RedisValue[] values = items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return _database.SetAddAsync(Key, values, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetContainsAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> ContainsAsync(TElement item, CommandFlags flags = CommandFlags.None)
		{
			RedisValue value = RedisSerializer.Serialize(item);
			return _database.SetContainsAsync(Key, value, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetLengthAsync(RedisKey, CommandFlags)"/>
		public Task<long> LengthAsync(CommandFlags flags = CommandFlags.None)
		{
			return _database.SetLengthAsync(Key, flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public async Task<TElement[]> MembersAsync(CommandFlags flags = CommandFlags.None)
		{
			RedisValue[] values = await _database.SetMembersAsync(Key, flags);
			return Array.ConvertAll(values, (Converter<RedisValue, TElement>)(x => RedisSerializer.Deserialize<TElement>(x)!));
		}

		/// <inheritdoc cref="IDatabaseAsync.SetPopAsync(RedisKey, CommandFlags)"/>
		public async Task<TElement> PopAsync(CommandFlags flags = CommandFlags.None)
		{
			RedisValue value = await _database.SetPopAsync(Key, flags);
			return value.IsNull ? default! : RedisSerializer.Deserialize<TElement>(value)!;
		}

		/// <inheritdoc cref="IDatabaseAsync.SetPopAsync(RedisKey, Int64, CommandFlags)"/>
		public async Task<TElement[]> PopAsync(long count, CommandFlags flags = CommandFlags.None)
		{
			RedisValue[] values = await _database.SetPopAsync(Key, count, flags);
			return Array.ConvertAll<RedisValue, TElement>(values, x => RedisSerializer.Deserialize<TElement>(x)!);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> RemoveAsync(TElement item, CommandFlags flags = CommandFlags.None)
		{
			return _database.SetRemoveAsync(Key, RedisSerializer.Serialize(item), flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public Task<long> RemoveAsync(IEnumerable<TElement> items, CommandFlags flags = CommandFlags.None)
		{
			RedisValue[] values = items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return _database.SetRemoveAsync(Key, values, flags);
		}
	}

	/// <summary>
	/// Extension methods for sets
	/// </summary>
	public static class RedisSetExtensions
	{
		/// <summary>
		/// Creates a version of this set which modifies a transaction rather than the direct DB
		/// </summary>
		public static RedisSet<TElement> With<TElement>(this ITransaction transaction, RedisSet<TElement> set)
		{
			return new RedisSet<TElement>(transaction, set.Key);
		}
	}
}
