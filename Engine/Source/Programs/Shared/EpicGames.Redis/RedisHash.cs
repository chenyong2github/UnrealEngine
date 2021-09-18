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
	public readonly struct RedisHash<TName, TValue>
	{
		internal readonly IDatabaseAsync Database;

		/// <summary>
		/// The key for the hash
		/// </summary>
		public readonly RedisKey Key { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RedisHash(IDatabaseAsync Database, RedisKey Key)
		{
			this.Database = Database;
			this.Key = Key;
		}

		/// <inheritdoc cref="IDatabaseAsync.HashDeleteAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> DeleteAsync(TName Name, CommandFlags Flags = CommandFlags.None)
		{
			return Database.HashDeleteAsync(Key, RedisSerializer.Serialize(Name), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashDeleteAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public Task<long> DeleteAsync(IEnumerable<TName> Names, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] NameArray = Names.Select(x => RedisSerializer.Serialize(x)).ToArray();
			return Database.HashDeleteAsync(Key, NameArray, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashExistsAsync(RedisKey, RedisValue, CommandFlags)"/>
		public Task<bool> ExistsAsync(TName Name, CommandFlags Flags = CommandFlags.None)
		{
			return Database.HashExistsAsync(Key, RedisSerializer.Serialize(Name), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashGetAsync(RedisKey, RedisValue, CommandFlags)"/>
		public async Task<TValue> GetAsync(TName Name, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue Value = await Database.HashGetAsync(Key, RedisSerializer.Serialize(Name), Flags);
			return Value.IsNull ? default(TValue)! : RedisSerializer.Deserialize<TValue>(Value);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashGetAsync(RedisKey, RedisValue[], CommandFlags)"/>
		public async Task<TValue[]> GetAsync(IEnumerable<TName> Names, CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] NameArray = Names.Select(x => RedisSerializer.Serialize(x)).ToArray();
			RedisValue[] ValueArray = await Database.HashGetAsync(Key, NameArray, Flags);
			return Array.ConvertAll(ValueArray, x => RedisSerializer.Deserialize<TValue>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.HashGetAllAsync(RedisKey, CommandFlags)"/>
		public async Task<HashEntry<TName, TValue>[]> GetAllAsync(CommandFlags Flags = CommandFlags.None)
		{
			HashEntry[] Entries = await Database.HashGetAllAsync(Key, Flags);
			return Array.ConvertAll(Entries, x => new HashEntry<TName, TValue>(RedisSerializer.Deserialize<TName>(x.Name), RedisSerializer.Deserialize<TValue>(x.Value)));
		}

		/// <inheritdoc cref="IDatabaseAsync.HashKeysAsync(RedisKey, CommandFlags)"/>
		public async Task<TName[]> KeysAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] NameArray = await Database.HashKeysAsync(Key, Flags);
			return Array.ConvertAll(NameArray, x => RedisSerializer.Deserialize<TName>(x));
		}

		/// <inheritdoc cref="IDatabaseAsync.HashLengthAsync(RedisKey, CommandFlags)"/>
		public Task<long> LengthAsync(CommandFlags Flags = CommandFlags.None)
		{
			return Database.HashLengthAsync(Key, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashSetAsync(RedisKey, RedisValue, RedisValue, When, CommandFlags)"/>
		public Task SetAsync(TName Name, TValue Value, When When = When.Always, CommandFlags Flags = CommandFlags.None)
		{
			return Database.HashSetAsync(Key, RedisSerializer.Serialize(Name), RedisSerializer.Serialize(Value), When, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashSetAsync(RedisKey, HashEntry[], CommandFlags)"/>
		public Task SetAsync(IEnumerable<HashEntry<TName, TValue>> Entries, CommandFlags Flags = CommandFlags.None)
		{
			return Database.HashSetAsync(Key, Entries.Select(x => (HashEntry)x).ToArray(), Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashValuesAsync(RedisKey, CommandFlags)"/>
		public async Task<TValue[]> ValuesAsync(CommandFlags Flags = CommandFlags.None)
		{
			RedisValue[] Values = await Database.HashValuesAsync(Key);
			return Array.ConvertAll(Values, x => RedisSerializer.Deserialize<TValue>(x));
		}
	}

	/// <inheritdoc cref="HashEntry"/>
	public readonly struct HashEntry<TName, TValue>
	{
		/// <inheritdoc cref="HashEntry.Name"/>
		public readonly TName Name { get; }

		/// <inheritdoc cref="HashEntry.Value"/>
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
	/// Extension methods for hashes
	/// </summary>
	public static class RedisHashExtensions
	{
		/// <inheritdoc cref="IDatabaseAsync.HashDecrementAsync(RedisKey, RedisValue, long, CommandFlags)"/>
		public static Task<long> DecrementAsync<TName>(this RedisHash<TName, long> Hash, TName Name, long Value = 1L, CommandFlags Flags = CommandFlags.None)
		{
			return Hash.Database.HashDecrementAsync(Hash.Key, RedisSerializer.Serialize<TName>(Name), Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashDecrementAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public static Task<double> DecrementAsync<TName>(this RedisHash<TName, long> Hash, TName Name, double Value = 1.0, CommandFlags Flags = CommandFlags.None)
		{
			return Hash.Database.HashDecrementAsync(Hash.Key, RedisSerializer.Serialize<TName>(Name), Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashIncrementAsync(RedisKey, RedisValue, long, CommandFlags)"/>
		public static Task<long> IncrementAsync<TName>(this RedisHash<TName, long> Hash, TName Name, long Value = 1L, CommandFlags Flags = CommandFlags.None)
		{
			return Hash.Database.HashIncrementAsync(Hash.Key, RedisSerializer.Serialize<TName>(Name), Value, Flags);
		}

		/// <inheritdoc cref="IDatabaseAsync.HashIncrementAsync(RedisKey, RedisValue, double, CommandFlags)"/>
		public static Task<double> IncrementAsync<TName>(this RedisHash<TName, long> Hash, TName Name, double Value = 1.0, CommandFlags Flags = CommandFlags.None)
		{
			return Hash.Database.HashIncrementAsync(Hash.Key, RedisSerializer.Serialize<TName>(Name), Value, Flags);
		}

		/// <summary>
		/// Creates a version of this set which modifies a transaction rather than the direct DB
		/// </summary>
		public static RedisHash<TName, TValue> With<TName, TValue>(this ITransaction Transaction, RedisHash<TName, TValue> Set)
		{
			return new RedisHash<TName, TValue>(Transaction, Set.Key);
		}
	}
}
