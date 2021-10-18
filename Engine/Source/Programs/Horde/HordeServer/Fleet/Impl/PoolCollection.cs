// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Common;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Collection of pool documents
	/// </summary>
	public class PoolCollection : IPoolCollection
	{
		class PoolDocument : IPool
		{
			// Properties
			[BsonId]
			public PoolId Id { get; set; }
			[BsonRequired]
			public string Name { get; set; } = null!;
			[BsonIgnoreIfNull]
			public Condition? Condition { get; set; }
			public List<AgentWorkspace> Workspaces { get; set; } = new List<AgentWorkspace>();
			public Dictionary<string, string> Properties { get; set; } = new Dictionary<string, string>();
			[BsonIgnoreIfDefault(true)]
			public bool EnableAutoscaling { get; set; } = true;
			public int? MinAgents { get; set; }
			public int? NumReserveAgents { get; set; }
			public DateTime? LastScaleUpTime { get; set; }
			public DateTime? LastScaleDownTime { get; set; }
			public int UpdateIndex { get; set; }

			// Read-only wrappers
			IReadOnlyList<AgentWorkspace> IPool.Workspaces => Workspaces;
			IReadOnlyDictionary<string, string> IPool.Properties => Properties;

			public PoolDocument()
			{
			}

			public PoolDocument(IPool Other)
			{
				Id = Other.Id;
				Name = Other.Name;
				Condition = Other.Condition;
				Workspaces.AddRange(Other.Workspaces);
				Properties = new Dictionary<string, string>(Other.Properties);
				EnableAutoscaling = Other.EnableAutoscaling;
				MinAgents = Other.MinAgents;
				NumReserveAgents = Other.NumReserveAgents;
				LastScaleUpTime = Other.LastScaleUpTime;
				LastScaleDownTime = Other.LastScaleDownTime;
				UpdateIndex = Other.UpdateIndex;
			}
		}

		/// <summary>
		/// Collection of pool documents
		/// </summary>
		IMongoCollection<PoolDocument> Pools;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		public PoolCollection(DatabaseService DatabaseService)
		{
			Pools = DatabaseService.GetCollection<PoolDocument>("Pools");
		}

		/// <inheritdoc/>
		public async Task<IPool> AddAsync(PoolId Id, string Name, Condition? Condition, bool? EnableAutoscaling, int? MinAgents, int? NumReserveAgents, IEnumerable<KeyValuePair<string, string>>? Properties)
		{
			PoolDocument Pool = new PoolDocument();
			Pool.Id = Id;
			Pool.Name = Name;
			Pool.Condition = Condition;
			if (EnableAutoscaling != null)
			{
				Pool.EnableAutoscaling = EnableAutoscaling.Value;
			}
			Pool.MinAgents = MinAgents;
			Pool.NumReserveAgents = NumReserveAgents;
			if (Properties != null)
			{
				Pool.Properties = new Dictionary<string, string>(Properties);
			}
			await Pools.InsertOneAsync(Pool);
			return Pool;
		}

		/// <inheritdoc/>
		public async Task<List<IPool>> GetAsync()
		{
			List<PoolDocument> Results = await Pools.Find(FilterDefinition<PoolDocument>.Empty).ToListAsync();
			return Results.ConvertAll<IPool>(x => x);
		}

		/// <inheritdoc/>
		public async Task<IPool?> GetAsync(PoolId Id)
		{
			return await Pools.Find(x => x.Id == Id).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<PoolId>> GetPoolIdsAsync()
		{
			ProjectionDefinition<PoolDocument, BsonDocument> Projection = Builders<PoolDocument>.Projection.Include(x => x.Id);
			List<BsonDocument> Results = await Pools.Find(FilterDefinition<PoolDocument>.Empty).Project(Projection).ToListAsync();
			return Results.ConvertAll(x => new PoolId(x.GetElement("_id").Value.AsString));
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteAsync(PoolId Id)
		{
			FilterDefinition<PoolDocument> Filter = Builders<PoolDocument>.Filter.Eq(x => x.Id, Id);
			DeleteResult Result = await Pools.DeleteOneAsync(Filter);
			return Result.DeletedCount > 0;
		}

		/// <summary>
		/// Attempts to update a pool, upgrading it to the latest document schema if necessary
		/// </summary>
		/// <param name="PoolToUpdate">Interface for the document to update</param>
		/// <param name="Transaction">The transaction to execute</param>
		/// <returns>New pool definition, or null on failure.</returns>
		async Task<IPool?> TryUpdateAsync(IPool PoolToUpdate, TransactionBuilder<PoolDocument> Transaction)
		{
			if (Transaction.IsEmpty)
			{
				return PoolToUpdate;
			}

			Transaction.Set(x => x.UpdateIndex, PoolToUpdate.UpdateIndex + 1);

			PoolDocument? MutablePool = PoolToUpdate as PoolDocument;
			if (MutablePool != null)
			{
				UpdateResult Result = await Pools.UpdateOneAsync(x => x.Id == PoolToUpdate.Id && x.UpdateIndex == PoolToUpdate.UpdateIndex, Transaction.ToUpdateDefinition());
				if (Result.ModifiedCount == 0)
				{
					return null;
				}

				Transaction.ApplyTo(MutablePool);
			}
			else
			{
				MutablePool = new PoolDocument(PoolToUpdate);
				Transaction.ApplyTo(MutablePool);

				ReplaceOneResult Result = await Pools.ReplaceOneAsync<PoolDocument>(x => x.Id == PoolToUpdate.Id && x.UpdateIndex == PoolToUpdate.UpdateIndex, MutablePool);
				if (Result.ModifiedCount == 0)
				{
					return null;
				}
			}
			return MutablePool;
		}

		/// <inheritdoc/>
		public Task<IPool?> TryUpdateAsync(IPool Pool, string? NewName, Condition? NewCondition, bool? NewEnableAutoscaling, int? NewMinAgents, int? NewNumReserveAgents, List<AgentWorkspace>? NewWorkspaces, Dictionary<string, string?>? NewProperties, DateTime? LastScaleUpTime, DateTime? LastScaleDownTime)
		{
			TransactionBuilder<PoolDocument> Transaction = new TransactionBuilder<PoolDocument>();
			if (NewName != null)
			{
				Transaction.Set(x => x.Name, NewName);
			}
			if (NewCondition != null)
			{
				if (NewCondition.IsEmpty())
				{
					Transaction.Unset(x => x.Condition!);
				}
				else
				{
					Transaction.Set(x => x.Condition, NewCondition);
				}
			}
			if (NewEnableAutoscaling != null)
			{
				if (NewEnableAutoscaling.Value)
				{
					Transaction.Unset(x => x.EnableAutoscaling);
				}
				else
				{
					Transaction.Set(x => x.EnableAutoscaling, NewEnableAutoscaling.Value);
				}
			}
			if (NewMinAgents != null)
			{
				if (NewMinAgents.Value < 0)
				{
					Transaction.Unset(x => x.MinAgents!);
				}
				else
				{
					Transaction.Set(x => x.MinAgents, NewMinAgents.Value);
				}
			}
			if (NewNumReserveAgents != null)
			{
				if (NewNumReserveAgents.Value < 0)
				{
					Transaction.Unset(x => x.NumReserveAgents!);
				}
				else
				{
					Transaction.Set(x => x.NumReserveAgents, NewNumReserveAgents.Value);
				}
			}
			if (NewWorkspaces != null)
			{
				Transaction.Set(x => x.Workspaces, NewWorkspaces);
			}
			if (NewProperties != null)
			{
				Transaction.UpdateDictionary(x => x.Properties, NewProperties);
			}
			if (LastScaleUpTime != null)
			{
				Transaction.Set(x => x.LastScaleUpTime, LastScaleUpTime);
			}
			if (LastScaleDownTime != null)
			{
				Transaction.Set(x => x.LastScaleDownTime, LastScaleDownTime);
			}
			return TryUpdateAsync(Pool, Transaction);
		}
	}
}
