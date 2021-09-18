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
		/// <param name="Key"></param>
		public RedisSet(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.SetAddAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task AddAsync(TElement Value, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SetAddAsync(Key, RedisSerializer.Serialize(Value), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task RemoveAsync(TElement Value, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SetRemoveAsync(Key, RedisSerializer.Serialize(Value), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public async Task<TElement[]> MembersAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.SetMembersAsync(Key, Flags);
			return Array.ConvertAll(Values, (Converter<RedisValue, TElement>)(x => RedisSerializer.Deserialize<TElement>(x)));
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
		/// <param name="Transaction"></param>
		/// <returns></returns>
		public static RedisSet<TElement> With<TElement>(this ITransaction Transaction, RedisSet<TElement> Set)
		{
			return new RedisSet<TElement>(Transaction, Set.Key);
		}
	}
}
