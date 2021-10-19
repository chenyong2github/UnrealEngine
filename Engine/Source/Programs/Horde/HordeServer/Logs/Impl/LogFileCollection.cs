// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Wrapper around the jobs collection in a mongo DB
	/// </summary>
	public class LogFileCollection : ILogFileCollection
	{
		class LogChunkDocument : ILogChunk
		{
			public long Offset { get; set; }
			public int Length { get; set; }
			public int LineIndex { get; set; }

			[BsonIgnoreIfNull]
			public string? Server { get; set; }

			[BsonConstructor]
			public LogChunkDocument()
			{
			}

			public LogChunkDocument(LogChunkDocument Other)
			{
				Offset = Other.Offset;
				Length = Other.Length;
				LineIndex = Other.LineIndex;
				Server = Other.Server;
			}

			public LogChunkDocument Clone()
			{
				return (LogChunkDocument)MemberwiseClone();
			}
		}

		class LogFileDocument : ILogFile
		{
			[BsonRequired, BsonId]
			public LogId Id { get; set; }

			[BsonRequired]
			public JobId JobId { get; set; }

			public ObjectId? SessionId { get; set; }
			public LogType Type { get; set; }

			[BsonIgnoreIfNull]
			public int? MaxLineIndex { get; set; }

			[BsonIgnoreIfNull]
			public long? IndexLength { get; set; }

			public List<LogChunkDocument> Chunks { get; set; } = new List<LogChunkDocument>();

			[BsonRequired]
			public int UpdateIndex { get; set; }

			IReadOnlyList<ILogChunk> ILogFile.Chunks => Chunks;

			[BsonConstructor]
			private LogFileDocument()
			{
			}

			public LogFileDocument(JobId JobId, ObjectId? SessionId, LogType Type)
			{
				this.Id = LogId.GenerateNewId();
				this.JobId = JobId;
				this.SessionId = SessionId;
				this.Type = Type;
				this.MaxLineIndex = 0;
			}

			public LogFileDocument Clone()
			{
				LogFileDocument Document = (LogFileDocument)MemberwiseClone();
				Document.Chunks = Document.Chunks.ConvertAll(x => x.Clone());
				return Document;
			}
		}

		/// <summary>
		/// The jobs collection
		/// </summary>
		IMongoCollection<LogFileDocument> LogFiles;

		/// <summary>
		/// Hostname for the current server
		/// </summary>
		string HostName;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public LogFileCollection(DatabaseService DatabaseService)
		{
			LogFiles = DatabaseService.GetCollection<LogFileDocument>("LogFiles");
			this.HostName = Dns.GetHostName();
		}

		/// <inheritdoc/>
		public async Task<ILogFile> CreateLogFileAsync(JobId JobId, ObjectId? SessionId, LogType Type)
		{
			LogFileDocument NewLogFile = new LogFileDocument(JobId, SessionId, Type);
			await LogFiles.InsertOneAsync(NewLogFile);
			return NewLogFile;
		}

		/// <inheritdoc/>
		public async Task<ILogFile?> TryAddChunkAsync(ILogFile LogFileInterface, long Offset, int LineIndex)
		{
			LogFileDocument LogFile = ((LogFileDocument)LogFileInterface).Clone();

			int ChunkIdx = LogFile.Chunks.GetChunkForOffset(Offset) + 1;

			LogChunkDocument Chunk = new LogChunkDocument();
			Chunk.Offset = Offset;
			Chunk.LineIndex = LineIndex;
			Chunk.Server = HostName;
			LogFile.Chunks.Insert(ChunkIdx, Chunk);

			UpdateDefinition<LogFileDocument> Update = Builders<LogFileDocument>.Update.Set(x => x.Chunks, LogFile.Chunks);
			if (ChunkIdx == LogFile.Chunks.Count - 1)
			{
				LogFile.MaxLineIndex = null;
				Update = Update.Unset(x => x.MaxLineIndex);
			}

			if (!await TryUpdateLogFileAsync(LogFile, Update))
			{
				return null;
			}

			return LogFile;
		}

		/// <inheritdoc/>
		public async Task<ILogFile?> TryCompleteChunksAsync(ILogFile LogFileInterface, IEnumerable<CompleteLogChunkUpdate> ChunkUpdates)
		{
			LogFileDocument LogFile = ((LogFileDocument)LogFileInterface).Clone();

			// Update the length of any complete chunks
			UpdateDefinitionBuilder<LogFileDocument> UpdateBuilder = Builders<LogFileDocument>.Update;
			List<UpdateDefinition<LogFileDocument>> Updates = new List<UpdateDefinition<LogFileDocument>>();
			foreach (CompleteLogChunkUpdate ChunkUpdate in ChunkUpdates)
			{
				LogChunkDocument Chunk = LogFile.Chunks[ChunkUpdate.Index];
				Chunk.Length = ChunkUpdate.Length;
				Updates.Add(UpdateBuilder.Set(x => x.Chunks[ChunkUpdate.Index].Length, ChunkUpdate.Length));

				if (ChunkUpdate.Index == LogFile.Chunks.Count - 1)
				{
					LogFile.MaxLineIndex = Chunk.LineIndex + ChunkUpdate.LineCount;
					Updates.Add(UpdateBuilder.Set(x => x.MaxLineIndex, LogFile.MaxLineIndex));
				}
			}

			// Try to apply the updates
			if (Updates.Count > 0 && !await TryUpdateLogFileAsync(LogFile, UpdateBuilder.Combine(Updates)))
			{
				return null;
			}

			return LogFile;
		}

		/// <inheritdoc/>
		public async Task<ILogFile?> TryUpdateIndexAsync(ILogFile LogFileInterface, long NewIndexLength)
		{
			LogFileDocument LogFile = ((LogFileDocument)LogFileInterface).Clone();

			UpdateDefinition<LogFileDocument> Update = Builders<LogFileDocument>.Update.Set(x => x.IndexLength, NewIndexLength);
			if (!await TryUpdateLogFileAsync(LogFile, Update))
			{
				return null;
			}

			LogFile.IndexLength = NewIndexLength;
			return LogFile;
		}

		/// <inheritdoc/>
		private async Task<bool> TryUpdateLogFileAsync(LogFileDocument Current, UpdateDefinition<LogFileDocument> Update)
		{
			int PrevUpdateIndex = Current.UpdateIndex;
			Current.UpdateIndex++;
			UpdateResult Result = await LogFiles.UpdateOneAsync<LogFileDocument>(x => x.Id == Current.Id && x.UpdateIndex == PrevUpdateIndex, Update.Set(x => x.UpdateIndex, Current.UpdateIndex));
			return Result.ModifiedCount == 1;
		}

		/// <inheritdoc/>
		public async Task<ILogFile?> GetLogFileAsync(LogId LogFileId)
		{
			LogFileDocument LogFile = await LogFiles.Find<LogFileDocument>(x => x.Id == LogFileId).FirstOrDefaultAsync();
			return LogFile;
		}

		/// <inheritdoc/>
		public async Task<List<ILogFile>> GetLogFilesAsync(int? Index = null, int? Count = null)
		{
			IFindFluent<LogFileDocument, LogFileDocument> Query = LogFiles.Find(FilterDefinition<LogFileDocument>.Empty);
			if(Index != null)
			{
				Query = Query.Skip(Index.Value);
			}
			if(Count != null)
			{
				Query = Query.Limit(Count.Value);
			}

			List<LogFileDocument> Results = await Query.ToListAsync();
			return Results.ConvertAll<ILogFile>(x => x);
		}
	}
}
