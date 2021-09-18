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
	public readonly struct RedisString<TElement>
	{
		internal readonly IDatabaseAsync Database;

		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Database"></param>
		/// <param name="Key"></param>
		public RedisString(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.StringGetAsync(RedisKey, CommandFlags)"/>
		public async Task<TElement> GetAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.StringGetAsync(Key, Flags);
			return RedisSerializer.Deserialize<TElement>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.StringLengthAsync(RedisKey, CommandFlags)"/>
		public Task<long> LengthAsync(CommandFlags Flags = CommandFlags.None)
		{
			return Database.StringLengthAsync(Key, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.StringSetAsync(RedisKey, RedisValue, TimeSpan?, When, CommandFlags)"/>
		public Task SetAsync(TElement Value, TimeSpan? Expiry = null, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.StringSetAsync(Key, RedisSerializer.Serialize(Value), Expiry, When, Flags);
		}
	}

	/// <summary>
	/// Extension methods for sets
	/// </summary>
	public static class RedisStringExtensions
	{
		/// <inheritdoc cref="IDatabaseAsync.StringDecrementAsync(RedisKey, long, CommandFlags)"/>
		public static Task<long> DecrementAsync(this RedisString<long> String, long Value = 1L, CommandFlags Flags = CommandFlags.None)
		{
			return String.Database.StringDecrementAsync(String.Key, Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.StringDecrementAsync(RedisKey, double, CommandFlags)"/>
		public static Task<double> DecrementAsync(this RedisString<double> String, double Value = 1.0, CommandFlags Flags = CommandFlags.None)
		{
			return String.Database.StringDecrementAsync(String.Key, Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.StringDecrementAsync(RedisKey, double, CommandFlags)"/>
		public static Task<long> IncrementAsync(this RedisString<long> String, long Value = 1L, CommandFlags Flags = CommandFlags.None)
		{
			return String.Database.StringIncrementAsync(String.Key, Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.StringDecrementAsync(RedisKey, double, CommandFlags)"/>
		public static Task<double> IncrementAsync(this RedisString<double> String, double Value = 1.0, CommandFlags Flags = CommandFlags.None)
		{
			return String.Database.StringIncrementAsync(String.Key, Value, Flags);
		}

		/// <summary>
		/// Creates a version of this string which modifies a transaction rather than the direct DB
		/// </summary>
		public static RedisString<TElement> With<TElement>(this ITransaction Transaction, RedisString<TElement> Set)
		{
			return new RedisString<TElement>(Transaction, Set.Key);
		}
	}
}
