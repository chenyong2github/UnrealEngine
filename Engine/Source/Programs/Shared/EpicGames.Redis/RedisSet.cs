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
		readonly IDatabaseAsync Database;

		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RedisSet(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.SetAddAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> AddAsync(TElement Item, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SetAddAsync(Key, RedisSerializer.Serialize(Item), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetAddAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public Task<long> AddAsync(IEnumerable<TElement> Items, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = Items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.SetAddAsync(Key, Values, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetContainsAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> ContainsAsync(TElement Item, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = RedisSerializer.Serialize(Item);
			return Database.SetContainsAsync(Key, Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetLengthAsync(RedisKey, CommandFlags)"/>
		public Task<long> LengthAsync(CommandFlags Flags = CommandFlags.None)
		{
			return Database.SetLengthAsync(Key, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public async Task<TElement[]> MembersAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SetMembersAsync(Key, Flags);
			return Array.ConvertAll(Values, (Converter<RedisValue, TElement>)(x => RedisSerializer.Deserialize<TElement>(x)));
		}

		/// <inheritdoc cref="IDatabaseAsync.SetPopAsync(RedisKey, CommandFlags)"/>
		public async Task<TElement> PopAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.SetPopAsync(Key, Flags);
			return Value.IsNull ? default(TElement)! : RedisSerializer.Deserialize<TElement>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetPopAsync(RedisKey, long, CommandFlags)"/>
		public async Task<TElement[]> PopAsync(long Count, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SetPopAsync(Key, Count, Flags);
			return Array.ConvertAll<RedisValue, TElement>(Values, x => RedisSerializer.Deserialize<TElement>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> RemoveAsync(TElement Item, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SetRemoveAsync(Key, RedisSerializer.Serialize(Item), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public Task<long> RemoveAsync(IEnumerable<TElement> Items, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = Items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.SetRemoveAsync(Key, Values, Flags);
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
		public static RedisSet<TElement> With<TElement>(this ITransaction Transaction, RedisSet<TElement> Set)
		{
			return new RedisSet<TElement>(Transaction, Set.Key);
		}
	}
}
