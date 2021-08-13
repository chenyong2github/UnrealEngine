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
	public readonly struct RedisSet<TElement> : IEquatable<RedisSet<TElement>>
	{
		/// <summary>
		/// The key for the list
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Key"></param>
		public RedisSet(RedisKey Key) => this.Key = Key;

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is RedisSet<TElement> Set && Key == Set.Key;

		/// <inheritdoc/>
		public bool Equals(RedisSet<TElement> Other) => Key == Other.Key;

		/// <inheritdoc/>
		public override int GetHashCode() => Key.GetHashCode();

		/// <summary>Compares two instances for equality</summary>
		public static bool operator ==(RedisSet<TElement> Left, RedisSet<TElement> Right) => Left.Key == Right.Key;

		/// <summary>Compares two instances for equality</summary>
		public static bool operator !=(RedisSet<TElement> Left, RedisSet<TElement> Right) => Left.Key != Right.Key;
	}

	/// <summary>
	/// Extension methods for <see cref="IDatabaseAsync"/>
	/// </summary>
	public static class RedisSetExtensions
	{
		/// <inheritdoc cref="IDatabaseAsync.SetAddAsync(RedisKey, RedisValue, CommandFlags)"/>
		public static Task SetAddAsync<T>(this IDatabaseAsync Database, RedisSet<T> Set, T Value, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SetAddAsync(Set.Key, RedisSerializer.Serialize(Value), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.SetRemoveAsync(RedisKey, RedisValue, CommandFlags)"/>
		public static Task SetRemoveAsync<T>(this IDatabaseAsync Database, RedisSet<T> Set, T Value, CommandFlags Flags = CommandFlags.None)
		{
			return Database.SetRemoveAsync(Set.Key, RedisSerializer.Serialize(Value), Flags);
		}
	}
}
