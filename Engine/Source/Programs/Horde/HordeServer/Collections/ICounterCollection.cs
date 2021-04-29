// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Collection of counter objects. Counters are named values stored in the database which can be atomically incremented.
	/// </summary>
	public interface ICounterCollection
	{
		/// <summary>
		/// Increments a counter with the given name. The first value returned is 1.
		/// </summary>
		/// <param name="Name">Name of the counter</param>
		/// <returns></returns>
		Task<int> IncrementAsync(string Name);
	}
}
