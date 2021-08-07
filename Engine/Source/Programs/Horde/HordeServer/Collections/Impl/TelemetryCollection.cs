// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Collection of utilization data
	/// </summary>
	public class TelemetryCollection : ITelemetryCollection
	{
		class UtilizationDocument : IUtilizationTelemetry
		{
			public DateTime StartTime { get; set; }
			public DateTime FinishTime { get; set; }

			public int NumAgents { get; set; }
			public List<PoolUtilizationDocument> Pools { get; set; }
			IReadOnlyList<IPoolUtilizationTelemetry> IUtilizationTelemetry.Pools => Pools;

			public double AdminTime { get; set; }
			public int UpdateIndex { get; set; }

			[BsonConstructor]
			private UtilizationDocument()
			{
				Pools = new List<PoolUtilizationDocument>();
			}

			public UtilizationDocument(IUtilizationTelemetry Other)
			{
				this.StartTime = Other.StartTime;
				this.FinishTime = Other.FinishTime;
				this.NumAgents = Other.NumAgents;
				this.Pools = Other.Pools.ConvertAll(x => new PoolUtilizationDocument(x));
				this.AdminTime = Other.AdminTime;
			}
		}

		class PoolUtilizationDocument : IPoolUtilizationTelemetry
		{
			public PoolId PoolId { get; set; }
			public int NumAgents { get; set; }

			public List<StreamUtilizationDocument> Streams { get; set; } 
			IReadOnlyList<IStreamUtilizationTelemetry> IPoolUtilizationTelemetry.Streams => Streams;

			public double AdminTime { get; set; }
			public double OtherTime { get; set; }

			[BsonConstructor]
			private PoolUtilizationDocument()
			{
				Streams = new List<StreamUtilizationDocument>();
			}

			public PoolUtilizationDocument(IPoolUtilizationTelemetry Other)
			{
				this.PoolId = Other.PoolId;
				this.NumAgents = Other.NumAgents;
				this.Streams = Other.Streams.ConvertAll(x => new StreamUtilizationDocument(x));
				this.AdminTime = Other.AdminTime;
				this.OtherTime = Other.OtherTime;
			}
		}

		class StreamUtilizationDocument : IStreamUtilizationTelemetry
		{
			public StreamId StreamId { get; set; }
			public double Time { get; set; }

			[BsonConstructor]
			private StreamUtilizationDocument()
			{
			}

			public StreamUtilizationDocument(IStreamUtilizationTelemetry Other)
			{
				this.StreamId = Other.StreamId;
				this.Time = Other.Time;
			}
		}

		IMongoCollection<UtilizationDocument> Utilization;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Database"></param>
		public TelemetryCollection(DatabaseService Database)
		{
			Utilization = Database.GetCollection<UtilizationDocument>("Utilization");

			if (!Database.ReadOnlyMode)
			{
				Utilization.Indexes.CreateOne(new CreateIndexModel<UtilizationDocument>(Builders<UtilizationDocument>.IndexKeys.Ascending(x => x.FinishTime).Ascending(x => x.StartTime)));
			}
		}

		/// <inheritdoc/>
		public async Task AddUtilizationTelemetryAsync(IUtilizationTelemetry NewTelemetry)
		{
			await Utilization.InsertOneAsync(new UtilizationDocument(NewTelemetry));
		}

		/// <inheritdoc/>
		public async Task<List<IUtilizationTelemetry>> GetUtilizationTelemetryAsync(DateTime StartTimeUtc, DateTime FinishTimeUtc)
		{
			FilterDefinition<UtilizationDocument> Filter = Builders<UtilizationDocument>.Filter.Gte(x => x.FinishTime, StartTimeUtc) & Builders<UtilizationDocument>.Filter.Lte(x => x.StartTime, FinishTimeUtc);

			List<UtilizationDocument> Documents = await Utilization.Find(Filter).ToListAsync();
			return Documents.ConvertAll<IUtilizationTelemetry>(x => x);
		}

		/// <inheritdoc/>
		public async Task<IUtilizationTelemetry?> GetLatestUtilizationTelemetryAsync()
		{
			return await Utilization.Find(FilterDefinition<UtilizationDocument>.Empty).SortByDescending(x => x.FinishTime).FirstOrDefaultAsync();
		}
	}
}
