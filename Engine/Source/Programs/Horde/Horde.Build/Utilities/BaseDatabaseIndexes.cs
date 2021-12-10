// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using MongoDB.Driver;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Base class for keeping track of indexes in MongoDB
	/// </summary>
	public abstract class BaseDatabaseIndexes
	{
		/// <summary>
		/// If database is in read-only mode (i.e no indexes can be created/modified)
		/// </summary>
		private bool DatabaseReadOnlyMode;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseReadOnlyMode"></param>
		protected BaseDatabaseIndexes(bool DatabaseReadOnlyMode)
		{
			this.DatabaseReadOnlyMode = DatabaseReadOnlyMode;
		}

		/// <summary>
		/// Create or get an index
		/// </summary>
		/// <param name="Collection">Name of collection</param>
		/// <param name="Name">Name of index (used for checking if index exists)</param>
		/// <param name="IndexDef">Type-specific index definition</param>
		/// <typeparam name="T">Document type</typeparam>
		/// <returns></returns>
		/// <exception cref="ArgumentException">If index cannot be found and database is in read-only mode</exception>
		protected string CreateOrGetIndex<T>(IMongoCollection<T> Collection, string Name, IndexKeysDefinition<T> IndexDef)
		{
			if (DatabaseReadOnlyMode)
			{
				if (Collection.Indexes.List().ToList().Find(x => x.TryGetString("name", out string? n) && n == Name) == null)
				{
					throw new ArgumentException($"Index with name {Name} not found (running in database read-only mode)");
				}
				return Name;
			}

			CreateIndexOptions IndexOptions = new() { Name = Name, Background = true };
			Collection.Indexes.CreateOne(new CreateIndexModel<T>(IndexDef, IndexOptions));
			return Name;
		}
	}

}
