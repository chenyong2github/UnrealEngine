// Copyright Epic Games, Inc. All Rights Reserved.

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
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using JobId = ObjectId<IJob>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Manages user documents
	/// </summary>
	class UserCollectionV1 : IUserCollection
	{
		class UserDocument : IUser, IUserClaims, IUserSettings
		{
			public UserId Id { get; set; }

			public ClaimDocument PrimaryClaim { get; set; } = null!;
			public List<ClaimDocument> Claims { get; set; } = new List<ClaimDocument>();

			[BsonDefaultValue(false), BsonIgnoreIfDefault]
			public bool EnableExperimentalFeatures { get; set; }

			[BsonDefaultValue(false), BsonIgnoreIfDefault]
			public bool EnableIssueNotifications { get; set; }

			public BsonValue DashboardSettings { get; set; } = BsonNull.Value;
			public List<JobId> PinnedJobIds { get; set; } = new List<JobId>();

			string IUser.Name => Claims.FirstOrDefault(x => String.Equals(x.Type, "name", StringComparison.Ordinal))?.Value ?? PrimaryClaim.Value;
			string IUser.Login => Claims.FirstOrDefault(x => String.Equals(x.Type, ClaimTypes.Name, StringComparison.Ordinal))?.Value ?? PrimaryClaim.Value;
			string? IUser.Email => Claims.FirstOrDefault(x => String.Equals(x.Type, ClaimTypes.Email, StringComparison.Ordinal))?.Value;

			UserId IUserClaims.UserId => Id;
			IReadOnlyList<IUserClaim> IUserClaims.Claims => Claims;

			UserId IUserSettings.UserId => Id;
			IReadOnlyList<JobId> IUserSettings.PinnedJobIds => PinnedJobIds;
		}

		class ClaimDocument : IUserClaim
		{
			public string Type { get; set; }
			public string Value { get; set; }

			private ClaimDocument()
			{
				this.Type = null!;
				this.Value = null!;
			}

			public ClaimDocument(string Type, string Value)
			{
				this.Type = Type;
				this.Value = Value;
			}

			public ClaimDocument(IUserClaim Other)
			{
				this.Type = Other.Type;
				this.Value = Other.Value;
			}
		}

		IMongoCollection<UserDocument> Users;

		/// <summary>
		/// Static constructor
		/// </summary>
		static UserCollectionV1()
		{
			BsonSerializer.RegisterDiscriminatorConvention(typeof(UserDocument), NullDiscriminatorConvention.Instance);
			BsonSerializer.RegisterDiscriminatorConvention(typeof(ClaimDocument), NullDiscriminatorConvention.Instance);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		public UserCollectionV1(DatabaseService DatabaseService)
		{
			Users = DatabaseService.GetCollection<UserDocument>("Users");

			if (!DatabaseService.ReadOnlyMode)
			{
				Users.Indexes.CreateOne(new CreateIndexModel<UserDocument>(Builders<UserDocument>.IndexKeys.Ascending(x => x.PrimaryClaim), new CreateIndexOptions { Unique = true }));
			}
		}

		/// <inheritdoc/>
		public async Task<IUser?> GetUserAsync(UserId Id)
		{
			return await Users.Find(x => x.Id == Id).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async ValueTask<IUser?> GetCachedUserAsync(UserId? Id)
		{
			if (Id == null)
			{
				return null;
			}
			else
			{
				return await GetUserAsync(Id.Value);
			}
		}

		/// <inheritdoc/>
		public async Task<List<IUser>> FindUsersAsync(IEnumerable<UserId>? Ids, string? NameRegex, int? Index, int? Count)
		{
			FilterDefinition<UserDocument> Filter = Builders<UserDocument>.Filter.In(x => x.Id, Ids);
			return await Users.Find(Filter).Range(Index, Count).ToListAsync<UserDocument, IUser>();
		}

		/// <inheritdoc/>
		public async Task<IUser?> FindUserByLoginAsync(string Login)
		{
			ClaimDocument PrimaryClaim = new ClaimDocument(ClaimTypes.Name, Login);
			return await Users.Find(x => x.PrimaryClaim == PrimaryClaim).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IUser> FindOrAddUserByLoginAsync(string Login, string? Name, string? Email)
		{
			ClaimDocument NewPrimaryClaim = new ClaimDocument(ClaimTypes.Name, Login);
			UpdateDefinition<UserDocument> Update = Builders<UserDocument>.Update.SetOnInsert(x => x.Id, UserId.GenerateNewId());
			return await Users.FindOneAndUpdateAsync<UserDocument>(x => x.PrimaryClaim == NewPrimaryClaim, Update, new FindOneAndUpdateOptions<UserDocument> { IsUpsert = true, ReturnDocument = ReturnDocument.After });
		}

		/// <inheritdoc/>
		public async Task<IUserClaims> GetClaimsAsync(UserId UserId)
		{
			return await Users.Find(x => x.Id == UserId).FirstOrDefaultAsync() ?? new UserDocument { Id = UserId };
		}

		/// <inheritdoc/>
		public async Task UpdateClaimsAsync(UserId UserId, IEnumerable<IUserClaim> Claims)
		{
			List<ClaimDocument> NewClaims = Claims.Select(x => new ClaimDocument(x)).ToList();
			await Users.FindOneAndUpdateAsync(x => x.Id == UserId, Builders<UserDocument>.Update.Set(x => x.Claims, NewClaims));
		}

		/// <inheritdoc/>
		public async Task<IUserSettings> GetSettingsAsync(UserId UserId)
		{
			return await Users.Find(x => x.Id == UserId).FirstOrDefaultAsync() ?? new UserDocument { Id = UserId };
		}

		/// <inheritdoc/>
		public async Task UpdateSettingsAsync(UserId UserId, bool? EnableExperimentalFeatures, bool? EnableIssueNotifications, BsonValue? DashboardSettings = null, IEnumerable<JobId>? AddPinnedJobIds = null, IEnumerable<JobId>? RemovePinnedJobIds = null)
		{
			if (AddPinnedJobIds != null)
			{
				foreach (JobId PinnedJobId in AddPinnedJobIds)
				{
					FilterDefinition<UserDocument> Filter = Builders<UserDocument>.Filter.Eq(x => x.Id, UserId) & Builders<UserDocument>.Filter.AnyNin<JobId>(x => x.PinnedJobIds, new[] { PinnedJobId });
					UpdateDefinition<UserDocument> Update = Builders<UserDocument>.Update.PushEach(x => x.PinnedJobIds, new[] { PinnedJobId }, -50);
					await Users.UpdateOneAsync(Filter, Update);
				}
			}

			List<UpdateDefinition<UserDocument>> Updates = new List<UpdateDefinition<UserDocument>>();
			if (EnableExperimentalFeatures != null)
			{
				Updates.Add(Builders<UserDocument>.Update.SetOrUnsetNull(x => x.EnableExperimentalFeatures, EnableExperimentalFeatures));
			}
			if (EnableIssueNotifications != null)
			{
				Updates.Add(Builders<UserDocument>.Update.SetOrUnsetNull(x => x.EnableIssueNotifications, EnableIssueNotifications));
			}
			if (DashboardSettings != null)
			{
				Updates.Add(Builders<UserDocument>.Update.Set(x => x.DashboardSettings, DashboardSettings));
			}
			if (RemovePinnedJobIds != null && RemovePinnedJobIds.Any())
			{
				Updates.Add(Builders<UserDocument>.Update.PullAll(x => x.PinnedJobIds, RemovePinnedJobIds));
			}
			if (Updates.Count > 0)
			{
				await Users.UpdateOneAsync<UserDocument>(x => x.Id == UserId, Builders<UserDocument>.Update.Combine(Updates));
			}
		}

		/// <summary>
		/// Enumerate all the documents in this collection
		/// </summary>
		/// <returns></returns>
		public async IAsyncEnumerable<(IUser, IUserClaims, IUserSettings)> EnumerateDocumentsAsync()
		{
			using (IAsyncCursor<UserDocument> Cursor = await Users.Find(FilterDefinition<UserDocument>.Empty).ToCursorAsync())
			{
				while (await Cursor.MoveNextAsync())
				{
					foreach (UserDocument Document in Cursor.Current)
					{
						if (Document.Claims.Count > 0)
						{
							yield return (Document, Document, Document);
						}
					}
				}
			}
		}
	}
}
