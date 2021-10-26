// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection.Metadata.Ecma335;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Stores a value that expires after a given time
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class LazyCachedValue<T> where T : class
	{
		/// <summary>
		/// The current value
		/// </summary>
		T? Value;

		/// <summary>
		/// Generator for the new value
		/// </summary>
		Func<T> Generator;

		/// <summary>
		/// Time since the value was updated
		/// </summary>
		Stopwatch Timer = Stopwatch.StartNew();

		/// <summary>
		/// Default expiry time
		/// </summary>
		TimeSpan DefaultMaxAge;

		/// <summary>
		/// Default constructor
		/// </summary>
		public LazyCachedValue(Func<T> Generator, TimeSpan MaxAge)
		{
			this.Generator = Generator;
			this.DefaultMaxAge = MaxAge;
		}

		/// <summary>
		/// Sets the new value
		/// </summary>
		/// <param name="Value">The value to store</param>
		public void Set(T Value)
		{
			this.Value = Value;
			Timer.Restart();
		}

		/// <summary>
		/// Tries to get the current value
		/// </summary>
		/// <returns>The cached value, if valid</returns>
		public T GetCached()
		{
			return GetCached(DefaultMaxAge);
		}

		/// <summary>
		/// Tries to get the current value
		/// </summary>
		/// <returns>The cached value, if valid</returns>
		public T GetCached(TimeSpan MaxAge)
		{
			T? Current = Value;
			if (Current == null || Timer.Elapsed > MaxAge)
			{
				Current = Generator();
				Set(Current);
			}
			return Current;
		}

		/// <summary>
		/// Gets the latest value, updating the cache
		/// </summary>
		/// <returns>The latest value</returns>
		public T GetLatest()
		{
			T NewValue = Generator();
			Set(NewValue);
			return NewValue;
		}
	}
}
