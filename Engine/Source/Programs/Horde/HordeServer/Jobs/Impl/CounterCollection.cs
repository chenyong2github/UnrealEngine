// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Collection of counter objects. Counters are named values stored in the database which can be atomically incremented.
	/// </summary>
	public class CounterCollection : ICounterCollection
	{
		class Counter
		{
			public string Id { get; set; } = String.Empty;
			public int Value { get; set; }
		}

		IMongoCollection<Counter> Counters;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">Database service instance</param>
		public CounterCollection(DatabaseService DatabaseService)
		{
			Counters = DatabaseService.GetCollection<Counter>("Counters");
		}

		/// <inheritdoc/>
		public async Task<int> IncrementAsync(string Name)
		{
			for (; ; )
			{
				Counter? Current = await Counters.Find(x => x.Id == Name).FirstOrDefaultAsync();
				if (Current == null)
				{
					Counter NewCounter = new Counter { Id = Name, Value = 1 };
					if (await Counters.InsertOneIgnoreDuplicatesAsync(NewCounter))
					{
						return NewCounter.Value;
					}
				}
				else
				{
					UpdateResult Result = await Counters.UpdateOneAsync(x => x.Id == Name && x.Value == Current.Value, Builders<Counter>.Update.Set(x => x.Value, Current.Value + 1));
					if (Result.ModifiedCount > 0)
					{
						return Current.Value + 1;
					}
				}
			}
		}
	}
}
