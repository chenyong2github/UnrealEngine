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

namespace HordeServer.Collections.Impl
{
	using AgentSoftwareVersion = StringId<IAgentSoftwareCollection>;

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

			[BsonIgnoreIfNull]
			public byte[]? Data { get; set; }

			[BsonIgnoreIfNull]
            public List<string>? ChunkIds { get; set; }

            [BsonConstructor]
			private AgentSoftwareDocument()
			{
				this.Id = null!;
				this.Data = null;
                this.ChunkIds = null;			
            }

			public AgentSoftwareDocument(string Id, byte[]? Data, List<string>? ChunkIds = null)
			{
				this.Id = Id;
				this.Data = Data;
                this.ChunkIds = ChunkIds;
            }
		}

		class ChunkDocument
		{
			[BsonId]
			public string Id { get; set; }

			public byte[] Data { get; set; } = null!;

			public string Version { get; set; }


            [BsonConstructor]
			private ChunkDocument()
			{
				this.Id = null!;
				this.Data = null!;
                this.Version = null!;
            }

			public ChunkDocument(string Id, string Version, byte[] Data)
			{
				this.Id = Id;
				this.Data = Data;
                this.Version = Version;
            }
		}

		// MongoDB document size limit is 16 megs, we chunk at this with a 1k buffer for anything else in document
		readonly int ChunkSize = 16 * 1024 * 1024 - 1024;



		/// <summary>
		/// Template documents
		/// </summary>
		IMongoCollection<AgentSoftwareDocument> Collection;

		/// <summary>
		/// Agent template chunks
		/// </summary>
		IMongoCollection<ChunkDocument> AgentChunks;


		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public AgentSoftwareCollection(DatabaseService DatabaseService)
		{
			Collection = DatabaseService.GetCollection<AgentSoftwareDocument>("AgentSoftware");
			AgentChunks = DatabaseService.GetCollection<ChunkDocument>("AgentSoftwareChunks");
		}

		/// <inheritdoc/>
		public async Task<bool> AddAsync(string Version, byte[] Data)
		{

			if (await ExistsAsync(Version))
			{
                return false;
            }

            AgentSoftwareDocument Document;

            // Check if agent will fit in document, otherwise we need to chunk
            if (Data.Length <= ChunkSize)
			{
				Document = new AgentSoftwareDocument(Version, Data);
				await Collection.InsertOneAsync(Document);
                return true;
            }

			int TotalRead = 0;            
            byte[] Buffer = new byte[ChunkSize];            
			List<ChunkDocument> Chunks = new List<ChunkDocument>();
            using (MemoryStream ChunkStream = new MemoryStream(Data))
            {
                for (; ; )
                {
					int BytesRead = ChunkStream.Read(Buffer, 0, ChunkSize);
					Chunks.Add(new ChunkDocument(ObjectId.GenerateNewId().ToString(), Version, Buffer.Take(BytesRead).ToArray()));
					TotalRead += BytesRead;
					if (TotalRead == Data.Length)
					{
						break;
					}
                }
            }

            // insert the chunks
            await AgentChunks.InsertManyAsync(Chunks);

            Document = new AgentSoftwareDocument(Version, null, Chunks.Select( Chunk => Chunk.Id).ToList());
			await Collection.InsertOneAsync(Document);
            return true;
        }

		/// <inheritdoc/>
		public async Task<bool> ExistsAsync(string Version)
		{
			return await Collection.Find(x => x.Id == Version).AnyAsync();
		}

		/// <inheritdoc/>
		public async Task<bool> RemoveAsync(string Version)
		{
			// Delete any chunks
			await AgentChunks.DeleteManyAsync(x => x.Version == Version);

			DeleteResult Result = await Collection.DeleteOneAsync(x => x.Id == Version);			
			return Result.DeletedCount > 0;
		}

		/// <inheritdoc/>
		public async Task<byte[]?> GetAsync(string Version)
		{			
			IFindFluent<AgentSoftwareDocument, AgentSoftwareDocument> Query = Collection.Find(x => x.Id == Version);
			AgentSoftwareDocument? Software = await Query.FirstOrDefaultAsync();

			if (Software == null)
			{
                return null;
            }

			if (Software.Data != null && Software.Data.Length > 0)
			{
                return Software.Data;
            }

			if (Software.ChunkIds != null)
			{
				FilterDefinitionBuilder<ChunkDocument> FilterBuilder = Builders<ChunkDocument>.Filter;
				FilterDefinition<ChunkDocument> Filter = FilterBuilder.Empty;
				Filter &= FilterBuilder.In(x => x.Id, Software.ChunkIds);

				IFindFluent<ChunkDocument, ChunkDocument> ChunkQuery = AgentChunks.Find(x => Software.ChunkIds.Contains(x.Id));				
				List<ChunkDocument> Chunks = await ChunkQuery.ToListAsync();

				using (MemoryStream DataStream = new MemoryStream())
				{
					foreach (string ChunkId in Software.ChunkIds)
					{
                        DataStream.Write(Chunks.First(x => x.Id == ChunkId).Data);
                    }

                    return DataStream.ToArray();
                }
			}

            return null;
        }
	}
}
