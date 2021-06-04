// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of agent software documents
	/// </summary>
	public class AgentSoftwareCollection : IAgentSoftwareCollection
	{
		/// <summary>
		/// Information about agent software
		/// </summary>
		class AgentSoftwareDocument
		{
			[BsonId]
			public string Id { get; set; }
			public byte[] Data { get; set; } = null!;

			[BsonConstructor]
			private AgentSoftwareDocument()
			{
				this.Id = null!;
				this.Data = null!;
			}

			public AgentSoftwareDocument(string Id, byte[] Data)
			{
				this.Id = Id;
				this.Data = Data;
			}
		}

		/// <summary>
		/// Template documents
		/// </summary>
		IMongoCollection<AgentSoftwareDocument> Collection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public AgentSoftwareCollection(DatabaseService DatabaseService)
		{
			Collection = DatabaseService.GetCollection<AgentSoftwareDocument>("AgentSoftware");
		}

		/// <inheritdoc/>
		public async Task<bool> AddAsync(string Version, byte[] Data)
		{
			AgentSoftwareDocument Document = new AgentSoftwareDocument(Version, Data);
			return await Collection.InsertOneIgnoreDuplicatesAsync(Document);
		}

		/// <inheritdoc/>
		public async Task<bool> ExistsAsync(string Version)
		{
			return await Collection.Find(x => x.Id == Version).AnyAsync();
		}

		/// <inheritdoc/>
		public async Task<bool> RemoveAsync(string Version)
		{
			DeleteResult Result = await Collection.DeleteOneAsync(x => x.Id == Version);
			return Result.DeletedCount > 0;
		}

		/// <inheritdoc/>
		public async Task<byte[]?> GetAsync(string Version)
		{
			IFindFluent<AgentSoftwareDocument, AgentSoftwareDocument> Query = Collection.Find(x => x.Id == Version);
			AgentSoftwareDocument? Software = await Query.FirstOrDefaultAsync();
			return Software?.Data;
		}
	}
}
