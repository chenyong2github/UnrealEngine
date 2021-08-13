// Copyright Epic Games, Inc. All Rights Reserved.

using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace EpicGames.Redis
{
	/// <summary>
	/// Extension methods for Redis
	/// </summary>
	public static class RedisExtensions
	{
		/// <summary>
		/// Convert a RedisValue to a RedisKey
		/// </summary>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static RedisKey AsKey(this RedisValue Value)
		{
			return (byte[])Value;
		}

		/// <summary>
		/// Convert a RedisKey to a RedisValue
		/// </summary>
		/// <param name="Key"></param>
		/// <returns></returns>
		public static RedisValue AsValue(this RedisKey Key)
		{
			return (byte[])Key;
		}
	}
}
