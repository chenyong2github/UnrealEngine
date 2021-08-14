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
	/// Represents a typed Redis hash with a given key and value
	/// </summary>
	/// <typeparam name="TName">The key type for the hash</typeparam>
	/// <typeparam name="TValue">The value type for the hash</typeparam>
	public readonly struct RedisHash<TName, TValue> : IEquatable<RedisHash<TName, TValue>>
	{
		/// <summary>
		/// The key for the hash
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Key"></param>
		public RedisHash(RedisKey Key) => this.Key = Key;

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is RedisHash<TName, TValue> List && Key == List.Key;

		/// <inheritdoc/>
		public bool Equals(RedisHash<TName, TValue> Other) => Key == Other.Key;

		/// <inheritdoc/>
		public override int GetHashCode() => Key.GetHashCode();

		/// <summary>Compares two instances for equality</summary>
		public static bool operator ==(RedisHash<TName, TValue> Left, RedisHash<TName, TValue> Right) => Left.Key == Right.Key;

		/// <summary>Compares two instances for equality</summary>
		public static bool operator !=(RedisHash<TName, TValue> Left, RedisHash<TName, TValue> Right) => Left.Key != Right.Key;
	}

	/// <inheritdoc cref="HashEntry"/>
	public readonly struct HashEntry<TName, TValue>
	{
		/// <inheritdoc cref="HashEntry.Name">
		public readonly TName Name { get; }

		/// <inheritdoc cref="HashEntry.Value">
		public readonly TValue Value { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		public HashEntry(TName Name, TValue Value)
		{
			this.Name = Name;
			this.Value = Value;
		}

		/// <summary>
		/// Implicit conversion to a <see cref="HashEntry"/>
		/// </summary>
		/// <param name="Entry"></param>
		public static implicit operator HashEntry(HashEntry<TName, TValue> Entry)
		{
			return new HashEntry(RedisSerializer.Serialize(Entry.Name), RedisSerializer.Serialize(Entry.Value));
		}

		/// <summary>
		/// Implicit conversion to a <see cref="KeyValuePair{TName, TValue}"/>
		/// </summary>
		/// <param name="Entry"></param>
		public static implicit operator KeyValuePair<TName, TValue>(HashEntry<TName, TValue> Entry)
		{
			return new KeyValuePair<TName, TValue>(Entry.Name, Entry.Value);
		}
	}

	/// <summary>
	/// Extension methods for typed lists
	/// </summary>
	public static class RedisHashExtensions
	{
		/// <inheritdoc cref="IDatabaseAsync.HashDeleteAsync(RedisKey, RedisValue, CommandFlags)"/>
		public static Task<bool> HashDeleteAsync<TName, TValue>(this IDatabase Redis, RedisHash<TName, TValue> Hash, TName Name, CommandFlags Flags = CommandFlags.None)
		{
			return Redis.HashDeleteAsync(Hash.Key, RedisSerializer.Serialize(Name), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashDeleteAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public static Task<long> HashDeleteAsync<TName, TValue>(this IDatabase Redis, RedisHash<TName, TValue> Hash, TName[] Names, CommandFlags Flags = CommandFlags.None)
		{
			return Redis.HashDeleteAsync(Hash.Key, Array.ConvertAll(Names, x => RedisSerializer.Serialize(x)), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashGetAllAsync(RedisKey, CommandFlags)"/>
		public static async Task<HashEntry<TName, TValue>[]> HashGetAllAsync<TName, TValue>(this IDatabase Redis, RedisHash<TName, TValue> Hash, CommandFlags Flags = CommandFlags.None)
		{
			HashEntry[] Entries = await Redis.HashGetAllAsync(Hash.Key, Flags);
			return Array.ConvertAll(Entries, x => new HashEntry<TName, TValue>(RedisSerializer.Deserialize<TName>(x.Name), RedisSerializer.Deserialize<TValue>(x.Value)));
		}

		/// <inheritdoc cref="IDatabaseAsync.HashLengthAsync(RedisKey, CommandFlags)"/>
		public static Task<long> HashLengthAsync<TName, TValue>(this IDatabase Redis, RedisHash<TName, TValue> Hash, CommandFlags Flags = CommandFlags.None)
		{
			return Redis.HashLengthAsync(Hash.Key, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashSetAsync(RedisKey, RedisValue, RedisValue, When, CommandFlags)"/>
		public static Task HashSetAsync<TName, TValue>(this IDatabase Redis, RedisHash<TName, TValue> Hash, TName Name, TValue Value, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Redis.HashSetAsync(Hash.Key, RedisSerializer.Serialize(Name), RedisSerializer.Serialize(Value), When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashSetAsync(RedisKey, HashEntry[], CommandFlags)"/>
		public static Task HashSetAsync<TName, TValue>(this IDatabase Redis, RedisHash<TName, TValue> Hash, HashEntry<TName, TValue>[] Entries, CommandFlags Flags = CommandFlags.None)
		{
			return Redis.HashSetAsync(Hash.Key, Array.ConvertAll(Entries, x => (HashEntry)x), Flags);
		}
	}
}
