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
	public readonly struct RedisString<TElement> : IEquatable<RedisString<TElement>>
	{
		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Key"></param>
		public RedisString(RedisKey Key) => this.Key = Key;

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is RedisString<TElement> String && Key == String.Key;

		/// <inheritdoc/>
		public bool Equals(RedisString<TElement> Other) => Key == Other.Key;

		/// <inheritdoc/>
		public override int GetHashCode() => Key.GetHashCode();

		/// <summary>Compares two instances for equality</summary>
		public static bool operator ==(RedisString<TElement> Left, RedisString<TElement> Right) => Left.Key == Right.Key;

		/// <summary>Compares two instances for equality</summary>
		public static bool operator !=(RedisString<TElement> Left, RedisString<TElement> Right) => Left.Key != Right.Key;
	}

	/// <summary>
	/// Extension methods for <see cref="IDatabaseAsync"/>
	/// </summary>
	public static class RedisStringExtensions
	{
		/// <inheritdoc cref="IDatabaseAsync.StringSetAsync(RedisKey, RedisValue, TimeSpan?, When, CommandFlags)"/>
		public static Task StringSetAsync<T>(this IDatabaseAsync Database, RedisString<T> Str, T Value, TimeSpan? Expiry = null, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.StringSetAsync(Str.Key, RedisSerializer.Serialize(Value), Expiry, When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.StringGetAsync(RedisKey, CommandFlags)"/>
		public static async Task<T> StringSetAsync<T>(this IDatabaseAsync Database, RedisString<T> Str, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.StringGetAsync(Str.Key, Flags);
			return RedisSerializer.Deserialize<T>(Value);
		}
	}
}
