// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using MongoDB.Driver.Core.Authentication;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Concrete implementation of IUgsMetadataCollection
	/// </summary>
	class UgsMetadataCollection : IUgsMetadataCollection
	{
		class UgsUserDataDocument : IUgsUserData
		{
			public string User { get; set; }

			[BsonIgnoreIfNull]
			public long? SyncTime { get; set; }

			[BsonIgnoreIfNull]
			public UgsUserVote? Vote { get; set; }

			[BsonIgnoreIfNull]
			public bool? Starred { get; set; }

			[BsonIgnoreIfNull]
			public bool? Investigating { get; set; }

			[BsonIgnoreIfNull]
			public string? Comment { get; set; }

			long? IUgsUserData.SyncTime => SyncTime;
			UgsUserVote IUgsUserData.Vote => Vote ?? UgsUserVote.None;

			public UgsUserDataDocument(string User)
			{
				this.User = User;
			}
		}

		class UgsBadgeDataDocument : IUgsBadgeData
		{
			public string Name { get; set; } = String.Empty;

			[BsonIgnoreIfNull]
			public Uri? Url { get; set; }

			public UgsBadgeState State { get; set; }
		}

		class UgsMetadataDocument : IUgsMetadata
		{
			public ObjectId Id { get; set; }
			public string Stream { get; set; }
			public int Change { get; set; }
			public string Project { get; set; } = String.Empty;
			public List<UgsUserDataDocument> Users { get; set; } = new List<UgsUserDataDocument>();
			public List<UgsBadgeDataDocument> Badges { get; set; } = new List<UgsBadgeDataDocument>();
			public int UpdateIndex { get; set; }
			public long UpdateTicks { get; set; }

			IReadOnlyList<IUgsUserData>? IUgsMetadata.Users => (Users.Count > 0)? Users : null;
			IReadOnlyList<IUgsBadgeData>? IUgsMetadata.Badges => (Badges.Count > 0) ? Badges : null;

			[BsonConstructor]
			private UgsMetadataDocument()
			{
				this.Stream = String.Empty;
			}

			public UgsMetadataDocument(string Stream, int Change, string Project)
			{
				this.Id = ObjectId.GenerateNewId();
				this.Stream = Stream;
				this.Change = Change;
				this.Project = Project;
			}
		}

		IMongoCollection<UgsMetadataDocument> Collection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">Database service instance</param>
		public UgsMetadataCollection(DatabaseService DatabaseService)
		{
			Collection = DatabaseService.GetCollection<UgsMetadataDocument>("UgsMetadata");
			
			if (!DatabaseService.ReadOnlyMode)
			{
				void CreateIndex()
				{
					Collection.Indexes.CreateOne(new CreateIndexModel<UgsMetadataDocument>(Builders<UgsMetadataDocument>.IndexKeys.Ascending(x => x.Stream).Descending(x => x.Change).Ascending(x => x.Project), new CreateIndexOptions { Unique = true }));
					Collection.Indexes.CreateOne(new CreateIndexModel<UgsMetadataDocument>(Builders<UgsMetadataDocument>.IndexKeys.Ascending(x => x.Stream).Descending(x => x.Change).Descending(x => x.UpdateTicks)));
				}
				
				try
				{
					CreateIndex();	
				}
				catch (MongoCommandException)
				{
					// If index creation fails, drop all indexes and retry.
					Collection.Indexes.DropAll();
					CreateIndex();
				}
			}
		}

		/// <summary>
		/// Find or add a document for the given change
		/// </summary>
		/// <param name="Stream">Stream containing the change</param>
		/// <param name="Change">The changelist number to add a document for</param>
		/// <param name="Project">Arbitrary identifier for this project</param>
		/// <returns>The metadata document</returns>
		public async Task<IUgsMetadata> FindOrAddAsync(string Stream, int Change, string? Project)
		{
			string NormalizedStream = GetNormalizedStream(Stream);
			string NormalizedProject = GetNormalizedProject(Project);
			for (; ; )
			{
				// Find an existing document
				UgsMetadataDocument? Existing = await Collection.Find(x => x.Stream == NormalizedStream && x.Change == Change && x.Project == NormalizedProject).FirstOrDefaultAsync();
				if (Existing != null)
				{
					return Existing;
				}

				// Try to insert a new document
				try
				{
					UgsMetadataDocument NewDocument = new UgsMetadataDocument(NormalizedStream, Change, NormalizedProject);
					await Collection.InsertOneAsync(NewDocument);
					return NewDocument;
				}
				catch (MongoWriteException Ex)
				{
					if (Ex.WriteError.Category != ServerErrorCategory.DuplicateKey)
					{
						throw;
					}
				}
			}
		}

		/// <inheritdoc/>
		public async Task<IUgsMetadata> UpdateUserAsync(IUgsMetadata Metadata, string UserName, bool? Synced, UgsUserVote? Vote, bool? Investigating, bool? Starred, string? Comment)
		{
			UpdateDefinitionBuilder<UgsMetadataDocument> UpdateBuilder = Builders<UgsMetadataDocument>.Update;
			for (; ; )
			{
				UgsMetadataDocument Document = (UgsMetadataDocument)Metadata;

				int UserIdx = Document.Users.FindIndex(x => x.User.Equals(UserName, StringComparison.OrdinalIgnoreCase));
				if (UserIdx == -1)
				{
					// Create a new user entry
					UgsUserDataDocument UserData = new UgsUserDataDocument(UserName);
					UserData.SyncTime = (Synced == true) ? (long?)DateTime.UtcNow.Ticks : null;
					UserData.Vote = Vote;
					UserData.Investigating = (Investigating == true) ? Investigating : null;
					UserData.Starred = (Starred == true) ? Starred : null;
					UserData.Comment = Comment;

					if (await TryUpdateAsync(Document, UpdateBuilder.Push(x => x.Users, UserData)))
					{
						Document.Users.Add(UserData);
						return Metadata;
					}
				}
				else
				{
					// Update an existing entry
					UgsUserDataDocument UserData = Document.Users[UserIdx];

					long? NewSyncTime = Synced.HasValue ? (Synced.Value ? (long?)DateTime.UtcNow.Ticks : null) : UserData.SyncTime;
					UgsUserVote? NewVote = Vote.HasValue ? ((Vote.Value != UgsUserVote.None) ? Vote : null) : UserData.Vote;
					bool? NewInvestigating = Investigating.HasValue ? (Investigating.Value ? Investigating : null) : UserData.Investigating;
					bool? NewStarred = Starred.HasValue ? (Starred.Value ? Starred : null) : UserData.Starred;

					List<UpdateDefinition<UgsMetadataDocument>> Updates = new List<UpdateDefinition<UgsMetadataDocument>>();
					if (NewSyncTime != UserData.SyncTime)
					{
						Updates.Add(UpdateBuilder.Set(x => x.Users![UserIdx].SyncTime, NewSyncTime));
					}
					if (NewVote != UserData.Vote)
					{
						Updates.Add(UpdateBuilder.Set(x => x.Users![UserIdx].Vote, NewVote));
					}
					if (NewInvestigating != UserData.Investigating)
					{
						Updates.Add(UpdateBuilder.SetOrUnsetNull(x => x.Users![UserIdx].Investigating, NewInvestigating));
					}
					if (NewStarred != UserData.Starred)
					{
						Updates.Add(UpdateBuilder.SetOrUnsetNull(x => x.Users![UserIdx].Starred, NewStarred));
					}
					if (Comment != null)
					{
						Updates.Add(UpdateBuilder.Set(x => x.Users![UserIdx].Comment, Comment));
					}

					if (Updates.Count == 0 || await TryUpdateAsync(Document, UpdateBuilder.Combine(Updates)))
					{
						UserData.SyncTime = NewSyncTime;
						UserData.Vote = Vote;
						UserData.Investigating = NewInvestigating;
						UserData.Starred = NewStarred;
						UserData.Comment = Comment;
						return Metadata;
					}
				}

				// Update the document and try again
				Metadata = await FindOrAddAsync(Metadata.Stream, Metadata.Change, Metadata.Project);
			}
		}

		/// <inheritdoc/>
		public async Task<IUgsMetadata> UpdateBadgeAsync(IUgsMetadata Metadata, string Name, Uri? Url, UgsBadgeState State)
		{
			UpdateDefinitionBuilder<UgsMetadataDocument> UpdateBuilder = Builders<UgsMetadataDocument>.Update;
			for (; ; )
			{
				UgsMetadataDocument Document = (UgsMetadataDocument)Metadata;

				// Update the document
				int BadgeIdx = Document.Badges.FindIndex(x => x.Name.Equals(Name, StringComparison.OrdinalIgnoreCase));
				if (BadgeIdx == -1)
				{
					// Create a new badge
					UgsBadgeDataDocument NewBadge = new UgsBadgeDataDocument();
					NewBadge.Name = Name;
					NewBadge.Url = Url;
					NewBadge.State = State;

					if (await TryUpdateAsync(Document, UpdateBuilder.Push(x => x.Badges, NewBadge)))
					{
						Document.Badges.Add(NewBadge);
						return Metadata;
					}
				}
				else
				{
					// Update an existing badge
					List<UpdateDefinition<UgsMetadataDocument>> Updates = new List<UpdateDefinition<UgsMetadataDocument>>();

					UgsBadgeDataDocument BadgeData = Document.Badges[BadgeIdx];
					if (Url != BadgeData.Url)
					{
						Updates.Add(UpdateBuilder.Set(x => x.Badges![BadgeIdx].Url, Url));
					}
					if (State != BadgeData.State)
					{
						Updates.Add(UpdateBuilder.Set(x => x.Badges![BadgeIdx].State, State));
					}

					if (Updates.Count == 0 || await TryUpdateAsync(Document, UpdateBuilder.Combine(Updates)))
					{
						BadgeData.Url = Url;
						BadgeData.State = State;
						return Metadata;
					}
				}

				// Update the document and try again
				Metadata = await FindOrAddAsync(Metadata.Stream, Metadata.Change, Metadata.Project);
			}
		}

		/// <inheritdoc/>
		public async Task<List<IUgsMetadata>> FindAsync(string Stream, int MinChange, int? MaxChange = null, long? MinTime = null)
		{
			FilterDefinitionBuilder<UgsMetadataDocument> FilterBuilder = Builders<UgsMetadataDocument>.Filter;

			string NormalizedStream = GetNormalizedStream(Stream);

			FilterDefinition<UgsMetadataDocument> Filter = FilterBuilder.Eq(x => x.Stream, NormalizedStream) & FilterBuilder.Gte(x => x.Change, MinChange);
			if (MaxChange != null)
			{
				Filter &= FilterBuilder.Lte(x => x.Change, MaxChange.Value);
			}
			if (MinTime != null)
			{
				Filter &= FilterBuilder.Gt(x => x.UpdateTicks, MinTime.Value);
			}

			List<UgsMetadataDocument> Documents = await Collection.Find(Filter).ToListAsync();
			return Documents.ConvertAll<IUgsMetadata>(x =>
			{
				// Remove polluting null entries. Need to find the source of these.
				x.Users = x.Users.Where(y => y != null).ToList();
				x.Badges = x.Badges.Where(y => y != null).ToList();
				return x;
			});
		}

		/// <summary>
		/// Normalize a stream argument
		/// </summary>
		/// <param name="Stream">The stream name</param>
		/// <returns>Normalized name</returns>
		[SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase")]
		private static string GetNormalizedStream(string Stream)
		{
			return Stream.ToLowerInvariant().TrimEnd('/');
		}

		/// <summary>
		/// Normalize a project argument
		/// </summary>
		/// <param name="Project">The project name</param>
		/// <returns>Normalized name</returns>
		[SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase")]
		private static string GetNormalizedProject(string? Project)
		{
			if(Project == null)
			{
				return String.Empty;
			}
			else
			{
				return Project.ToLowerInvariant();
			}
		}

		/// <summary>
		/// Try to update a metadata document
		/// </summary>
		/// <param name="Document">The document to update</param>
		/// <param name="Update">Update definition</param>
		/// <returns>True if the update succeeded</returns>
		private async Task<bool> TryUpdateAsync(UgsMetadataDocument Document, UpdateDefinition<UgsMetadataDocument> Update)
		{
			int NewUpdateIndex = Document.UpdateIndex + 1;
			Update = Update.Set(x => x.UpdateIndex, NewUpdateIndex);

			long NewUpdateTicks = DateTime.UtcNow.Ticks;
			Update = Update.Set(x => x.UpdateTicks, NewUpdateTicks);

			UpdateResult Result = await Collection.UpdateOneAsync(x => x.Change == Document.Change && x.UpdateIndex == Document.UpdateIndex, Update);
			if (Result.ModifiedCount > 0)
			{
				Document.UpdateIndex = NewUpdateIndex;
				Document.UpdateTicks = NewUpdateTicks;
				return true;
			}
			return false;
		}
	}
}
