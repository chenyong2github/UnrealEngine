// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using MongoDB.Driver;

namespace Horde.Build.Utilities
{
	/// <summary>
	/// Base class for keeping track of indexes in MongoDB
	/// </summary>
	public abstract class BaseDatabaseIndexes
	{
		/// <summary>
		/// If database is in read-only mode (i.e no indexes can be created/modified)
		/// </summary>
		private readonly bool _databaseReadOnlyMode;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="databaseReadOnlyMode"></param>
		protected BaseDatabaseIndexes(bool databaseReadOnlyMode)
		{
			_databaseReadOnlyMode = databaseReadOnlyMode;
		}

		/// <summary>
		/// Create or get an index
		/// </summary>
		/// <param name="collection">Name of collection</param>
		/// <param name="name">Name of index (used for checking if index exists)</param>
		/// <param name="indexDef">Type-specific index definition</param>
		/// <typeparam name="T">Document type</typeparam>
		/// <returns></returns>
		/// <exception cref="ArgumentException">If index cannot be found and database is in read-only mode</exception>
		protected string CreateOrGetIndex<T>(IMongoCollection<T> collection, string name, IndexKeysDefinition<T> indexDef)
		{
			if (_databaseReadOnlyMode)
			{
				if (collection.Indexes.List().ToList().Find(x => x.TryGetString("name", out string? n) && n == name) == null)
				{
					throw new ArgumentException($"Index with name {name} not found (running in database read-only mode)");
				}
				return name;
			}

			CreateIndexOptions indexOptions = new() { Name = name, Background = true };
			collection.Indexes.CreateOne(new CreateIndexModel<T>(indexDef, indexOptions));
			return name;
		}
	}
}
