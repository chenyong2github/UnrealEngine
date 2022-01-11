// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Services;
using MongoDB.Bson.Serialization.Attributes;
using System.Threading.Tasks;

using HordeServer.Models;
using MongoDB.Driver;
using System;
using HordeServer.Utilities;
using System.Linq;

using MongoDB.Bson;
using System.Collections.Generic;

namespace HordeServer.Collections.Impl
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Collection of notice documents
	/// </summary>
	public class NoticeCollection : INoticeCollection
	{

		/// <summary>
		/// Document representing a notice 
		/// </summary>
		class NoticeDocument : INotice
		{
			[BsonRequired, BsonId]
			public ObjectId Id { get; set; }

			[BsonIgnoreIfNull]
			public UserId? UserId { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? StartTime { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? FinishTime { get; set; }

			public string Message { get; set; } = String.Empty;

			[BsonConstructor]
			private NoticeDocument()
			{
				
			}

			public NoticeDocument(ObjectId Id, string Message, UserId? UserId, DateTime? StartTime, DateTime? FinishTime)
			{
				this.Id = Id;
				this.Message = Message;
				this.UserId = UserId;
				this.StartTime = StartTime;
				this.FinishTime = FinishTime;
			}

		}

		readonly IMongoCollection<NoticeDocument> Notices;

		/// <summary>
		/// Constructor
		/// </summary>
		public NoticeCollection(DatabaseService DatabaseService)
		{
			Notices = DatabaseService.GetCollection<NoticeDocument>("Notices");
		}

		/// <inheritdoc/>
		public async Task<INotice?> AddNoticeAsync(string Message, UserId? UserId, DateTime? StartTime, DateTime? FinishTime)
		{
			NoticeDocument NewNotice = new NoticeDocument(ObjectId.GenerateNewId(), Message, UserId, StartTime, FinishTime);
			await Notices.InsertOneAsync(NewNotice);
			return NewNotice;
		}

		/// <inheritdoc/>
		public async Task<bool> UpdateNoticeAsync(ObjectId Id, string? Message, DateTime? StartTime, DateTime? FinishTime)
		{

			UpdateDefinitionBuilder<NoticeDocument> UpdateBuilder = Builders<NoticeDocument>.Update;
			List<UpdateDefinition<NoticeDocument>> Updates = new List<UpdateDefinition<NoticeDocument>>();

			if (Message != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Message, Message));
			}

			Updates.Add(UpdateBuilder.Set(x => x.StartTime, StartTime));

			Updates.Add(UpdateBuilder.Set(x => x.FinishTime, FinishTime));

			NoticeDocument? Document = await Notices.FindOneAndUpdateAsync(x => x.Id == Id, UpdateBuilder.Combine(Updates));

			return Document != null;
		}

		/// <inheritdoc/>
		public async Task<INotice?> GetNoticeAsync(ObjectId NoticeId)
		{
			return await Notices.Find(x => x.Id == NoticeId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<INotice>> GetNoticesAsync()
		{
			List<NoticeDocument> Results = await Notices.Find(x => true).ToListAsync();
			return Results.Select<NoticeDocument, INotice>(x => x).ToList();
		}


		/// <inheritdoc/>
		public async Task<bool> RemoveNoticeAsync(ObjectId Id)
		{
			DeleteResult Result = await Notices.DeleteOneAsync(x => x.Id == Id);
			return Result.DeletedCount > 0;
		}

	}

}

