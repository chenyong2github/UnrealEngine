// Copyright Epic Games, Inc. All Rights Reserved.

using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Redis
{
	/// <summary>
	/// Represents a typed Redis list with a given key
	/// </summary>
	/// <typeparam name="TElement">The type of element stored in the list</typeparam>
	public readonly struct RedisList<TElement> : IEquatable<RedisList<TElement>>
	{
		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Key"></param>
		public RedisList(RedisKey Key) => this.Key = Key;

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is RedisList<TElement> List && Key == List.Key;

		/// <inheritdoc/>
		public bool Equals(RedisList<TElement> Other) => Key == Other.Key;

		/// <inheritdoc/>
		public override int GetHashCode() => Key.GetHashCode();

		/// <summary>Compares two instances for equality</summary>
		public static bool operator ==(RedisList<TElement> Left, RedisList<TElement> Right) => Left.Key == Right.Key;

		/// <summary>Compares two instances for equality</summary>
		public static bool operator !=(RedisList<TElement> Left, RedisList<TElement> Right) => Left.Key != Right.Key;
	}

	/// <summary>
	/// Extension methods for typed lists
	/// </summary>
	public static class RedisListExtensions
	{
		/// <inheritdoc cref="IDatabaseAsync.ListLengthAsync(RedisKey, CommandFlags)"/>
		public static Task<long> ListLengthAsync<T>(this IDatabase Database, RedisList<T> List)
		{
			return Database.ListLengthAsync(List.Key);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPushAsync(RedisKey, RedisValue, When, CommandFlags)"/>
		public static Task<long> ListLeftPushAsync<T>(this IDatabase Database, RedisList<T> List, T Item, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.ListLeftPushAsync(List.Key, RedisSerializer.Serialize(Item), When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPushAsync(RedisKey, RedisValue[], When, CommandFlags)"/>
		public static Task<long> ListLeftPushAsync<T>(this IDatabase Database, RedisList<T> List, IEnumerable<T> Items, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = Items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.ListLeftPushAsync(List.Key, Values, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListLeftPopAsync(RedisKey, CommandFlags)">
		public static async Task<T?> ListLeftPopAsync<T>(this IDatabase Database, RedisList<T> List, CommandFlags Flags = CommandFlags.None) where T : class
		{
			RedisValue Value = await Database.ListLeftPopAsync(List.Key, Flags);
			return Value.IsNull ? null : RedisSerializer.Deserialize<T>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRangeAsync(RedisKey, CommandFlags)">
		public static async Task<T[]> ListRangeAsync<T>(this IDatabase Database, RedisList<T> List, long Start = 0, long Stop = -1, CommandFlags Flags = CommandFlags.None) where T : class
		{
			RedisValue[] Values = await Database.ListRangeAsync(List.Key, Start, Stop, Flags);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<T>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPushAsync(RedisKey, RedisValue, When, CommandFlags)"/>
		public static Task<long> ListRightPushAsync<T>(this IDatabase Database, RedisList<T> List, T Item, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.ListRightPushAsync(List.Key, RedisSerializer.Serialize(Item), When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListRightPushAsync(RedisKey, RedisValue[], When, CommandFlags)"/>
		public static Task<long> ListRightPushAsync<T>(this IDatabase Database, RedisList<T> List, IEnumerable<T> Items, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = Items.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.ListRightPushAsync(List.Key, Values, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.ListTrimAsync(RedisKey, long, long, CommandFlags)">
		public static async Task ListTrimAsync<T>(this IDatabase Database, RedisList<T> List, long Start, long Stop, CommandFlags Flags = CommandFlags.None) where T : class
		{
			await Database.ListTrimAsync(List.Key, Start, Stop, Flags);
		}
	}
}
