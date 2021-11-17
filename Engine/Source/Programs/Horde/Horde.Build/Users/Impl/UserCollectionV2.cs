// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
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
	class UserCollectionV2 : IUserCollection, IDisposable
	{
		class UserDocument : IUser
		{
			public UserId Id { get; set; }

			public string Name { get; set; }
			public string Login { get; set; }
			public string LoginUpper { get; set; }
			public string? Email { get; set; }
			public string? EmailUpper { get; set; }

			[BsonIgnoreIfDefault]
			public bool? Hidden { get; set; }

			[BsonConstructor]
			private UserDocument()
			{
				Name = null!;
				Login = null!;
				LoginUpper = null!;
			}

			public UserDocument(IUser Other)
				: this(Other.Id, Other.Name, Other.Login, Other.Email)
			{
			}

			public UserDocument(UserId Id, string Name, string Login, string? Email)
			{
				this.Id = Id;
				this.Name = Name;
				this.Login = Login;
				this.LoginUpper = Login.ToUpperInvariant();
				this.Email = Email;
				this.EmailUpper = Email?.ToUpperInvariant();
			}
		}

		class UserClaimsDocument : IUserClaims
		{
			public UserId Id { get; set; }
			public List<UserClaim> Claims { get; set; } = new List<UserClaim>();

			UserId IUserClaims.UserId => Id;
			IReadOnlyList<IUserClaim> IUserClaims.Claims => Claims;

			[BsonConstructor]
			private UserClaimsDocument()
			{
			}

			public UserClaimsDocument(UserId Id)
			{
				this.Id = Id;
			}

			public UserClaimsDocument(IUserClaims Other)
				: this(Other.UserId)
			{
				this.Claims.AddRange(Other.Claims.Select(x => new UserClaim(x)));
			}
		}

		class UserSettingsDocument : IUserSettings
		{
			public UserId Id { get; set; }

			[BsonDefaultValue(false), BsonIgnoreIfDefault]
			public bool EnableExperimentalFeatures { get; set; }

			[BsonDefaultValue(true), BsonIgnoreIfDefault]
			public bool EnableIssueNotifications { get; set; } = true;

			public BsonValue DashboardSettings { get; set; } = BsonNull.Value;
			public List<JobId> PinnedJobIds { get; set; } = new List<JobId>();

			UserId IUserSettings.UserId => Id;
			IReadOnlyList<JobId> IUserSettings.PinnedJobIds => PinnedJobIds;

			[BsonConstructor]
			private UserSettingsDocument()
			{
			}

			public UserSettingsDocument(UserId Id)
			{
				this.Id = Id;
			}

			public UserSettingsDocument(IUserSettings Other)
				: this(Other.UserId)
			{
				this.EnableExperimentalFeatures = Other.EnableExperimentalFeatures;
				this.EnableIssueNotifications = Other.EnableIssueNotifications;
				this.DashboardSettings = Other.DashboardSettings;
				this.PinnedJobIds = new List<JobId>(Other.PinnedJobIds);
			}
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
		IMongoCollection<UserClaimsDocument> UserClaims;
		IMongoCollection<UserSettingsDocument> UserSettings;

		ILogger<UserCollectionV2> Logger;

		MemoryCache UserCache;

		/// <summary>
		/// Static constructor
		/// </summary>
		static UserCollectionV2()
		{
			BsonSerializer.RegisterDiscriminatorConvention(typeof(UserDocument), NullDiscriminatorConvention.Instance);
			BsonSerializer.RegisterDiscriminatorConvention(typeof(UserClaimsDocument), NullDiscriminatorConvention.Instance);
			BsonSerializer.RegisterDiscriminatorConvention(typeof(UserSettingsDocument), NullDiscriminatorConvention.Instance);

			BsonSerializer.RegisterDiscriminatorConvention(typeof(ClaimDocument), NullDiscriminatorConvention.Instance);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="Logger"></param>
		public UserCollectionV2(DatabaseService DatabaseService, ILogger<UserCollectionV2> Logger)
		{
			this.Logger = Logger;

			Users = DatabaseService.GetCollection<UserDocument>("UsersV2");
			UserClaims = DatabaseService.GetCollection<UserClaimsDocument>("UserClaimsV2");
			UserSettings = DatabaseService.GetCollection<UserSettingsDocument>("UserSettingsV2");

			if (!DatabaseService.ReadOnlyMode)
			{
				Users.Indexes.CreateOne(new CreateIndexModel<UserDocument>(Builders<UserDocument>.IndexKeys.Ascending(x => x.LoginUpper), new CreateIndexOptions { Unique = true }));
				Users.Indexes.CreateOne(new CreateIndexModel<UserDocument>(Builders<UserDocument>.IndexKeys.Ascending(x => x.EmailUpper)));
			}

			MemoryCacheOptions Options = new MemoryCacheOptions();
			UserCache = new MemoryCache(Options);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			UserCache.Dispose();
		}

		/// <inheritdoc/>
		public async Task<IUser?> GetUserAsync(UserId Id)
		{
			IUser? User = await Users.Find(x => x.Id == Id).FirstOrDefaultAsync();
			using (ICacheEntry Entry = UserCache.CreateEntry(Id))
			{
				Entry.SetValue(User);
				Entry.SetSlidingExpiration(TimeSpan.FromMinutes(30.0));
			}
			return User;
		}

		/// <inheritdoc/>
		public async ValueTask<IUser?> GetCachedUserAsync(UserId? Id)
		{
			IUser? User;
			if(Id == null)
			{
				return null;
			}
			else if (UserCache.TryGetValue(Id.Value, out User))
			{
				return User;
			}
			else
			{
				return await GetUserAsync(Id.Value);
			}
		}

		/// <inheritdoc/>
		public async Task<List<IUser>> FindUsersAsync(IEnumerable<UserId>? Ids, string? NameRegex = null, int? Index = null, int? Count = null)
		{
			FilterDefinition<UserDocument> Filter = FilterDefinition<UserDocument>.Empty;
			if (Ids != null)
			{
				Filter &= Builders<UserDocument>.Filter.In(x => x.Id, Ids);
			}
			
			if (NameRegex != null)
			{
				BsonRegularExpression Regex = new BsonRegularExpression(NameRegex, "i");
				Filter &= Builders<UserDocument>.Filter.Regex(x => x.Name, Regex);
			}

			Filter &= Builders<UserDocument>.Filter.Ne(x => x.Hidden, true);

			return await Users.Find(Filter).Range(Index, Count ?? 100).ToListAsync<UserDocument, IUser>();
		}

		/// <inheritdoc/>
		public async Task<IUser?> FindUserByLoginAsync(string Login)
		{
			string LoginUpper = Login.ToUpperInvariant();
			return await Users.Find(x => x.LoginUpper == LoginUpper).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IUser> FindOrAddUserByLoginAsync(string Login, string? Name, string? Email)
		{
			UpdateDefinition<UserDocument> Update = Builders<UserDocument>.Update.SetOnInsert(x => x.Id, UserId.GenerateNewId()).SetOnInsert(x => x.Login, Login).Unset(x => x.Hidden);

			if (Name == null)
			{
				Update = Update.SetOnInsert(x => x.Name, Name ?? Login);
			}
			else
			{
				Update = Update.Set(x => x.Name, Name);
			}

			if (Email != null)
			{
				Update = Update.Set(x => x.Email, Email).Set(x => x.EmailUpper, Email.ToUpperInvariant());
			}

			string LoginUpper = Login.ToUpperInvariant();
			return await Users.FindOneAndUpdateAsync<UserDocument>(x => x.LoginUpper == LoginUpper, Update, new FindOneAndUpdateOptions<UserDocument, UserDocument> { IsUpsert = true, ReturnDocument = ReturnDocument.After });
		}

		/// <inheritdoc/>
		public async Task<IUserClaims> GetClaimsAsync(UserId UserId)
		{
			IUserClaims? Claims = await UserClaims.Find(x => x.Id == UserId).FirstOrDefaultAsync();
			if(Claims == null)
			{
				Claims = new UserClaimsDocument(UserId);
			}
			return Claims;
		}

		/// <inheritdoc/>
		public async Task UpdateClaimsAsync(UserId UserId, IEnumerable<IUserClaim> Claims)
		{
			UserClaimsDocument NewDocument = new UserClaimsDocument(UserId);
			NewDocument.Claims.AddRange(Claims.Select(x => new UserClaim(x)));
			await UserClaims.ReplaceOneAsync(x => x.Id == UserId, NewDocument, new ReplaceOptions { IsUpsert = true });
		}

		/// <inheritdoc/>
		public async Task<IUserSettings> GetSettingsAsync(UserId UserId)
		{
			IUserSettings? Settings = await UserSettings.Find(x => x.Id == UserId).FirstOrDefaultAsync();
			if (Settings == null)
			{
				Settings = new UserSettingsDocument(UserId);
			}
			return Settings;
		}

		/// <inheritdoc/>
		public async Task UpdateSettingsAsync(UserId UserId, bool? EnableExperimentalFeatures = null, bool? EnableIssueNotifications = null, BsonValue? DashboardSettings = null, IEnumerable<JobId>? AddPinnedJobIds = null, IEnumerable<JobId>? RemovePinnedJobIds = null)
		{
			List<UpdateDefinition<UserSettingsDocument>> Updates = new List<UpdateDefinition<UserSettingsDocument>>();
			if (EnableExperimentalFeatures != null)
			{
				Updates.Add(Builders<UserSettingsDocument>.Update.SetOrUnsetNull(x => x.EnableExperimentalFeatures, EnableExperimentalFeatures));
			}
			if (EnableIssueNotifications != null)
			{
				if (EnableIssueNotifications.Value)
				{
					Updates.Add(Builders<UserSettingsDocument>.Update.Unset(x => x.EnableIssueNotifications));
				}
				else
				{
					Updates.Add(Builders<UserSettingsDocument>.Update.Set(x => x.EnableIssueNotifications, false));
				}
			}
			if (DashboardSettings != null)
			{
				Updates.Add(Builders<UserSettingsDocument>.Update.Set(x => x.DashboardSettings, DashboardSettings));
			}
			if (AddPinnedJobIds != null && AddPinnedJobIds.Any())
			{
				Updates.Add(Builders<UserSettingsDocument>.Update.AddToSetEach(x => x.PinnedJobIds, AddPinnedJobIds));
			}
			if (RemovePinnedJobIds != null && RemovePinnedJobIds.Any())
			{
				Updates.Add(Builders<UserSettingsDocument>.Update.PullAll(x => x.PinnedJobIds, RemovePinnedJobIds));
			}
			if (Updates.Count > 0)
			{
				await UserSettings.UpdateOneAsync<UserSettingsDocument>(x => x.Id == UserId, Builders<UserSettingsDocument>.Update.Combine(Updates), new UpdateOptions { IsUpsert = true });
			}
		}

		/// <summary>
		/// Upgrade from V1 collection
		/// </summary>
		/// <param name="UserCollectionV1"></param>
		/// <returns></returns>
		public async Task ResaveDocumentsAsync(UserCollectionV1 UserCollectionV1)
		{
			await foreach ((IUser User, IUserClaims Claims, IUserSettings Settings) in UserCollectionV1.EnumerateDocumentsAsync())
			{
				try
				{
					await Users.ReplaceOneAsync(x => x.Id == User.Id, new UserDocument(User), new ReplaceOptions { IsUpsert = true });
					await UserClaims.ReplaceOneAsync(x => x.Id == User.Id, new UserClaimsDocument(Claims), new ReplaceOptions { IsUpsert = true });
					await UserSettings.ReplaceOneAsync(x => x.Id == User.Id, new UserSettingsDocument(Settings), new ReplaceOptions { IsUpsert = true });
					Logger.LogDebug("Updated user {UserId}", User.Id);
				}
				catch (MongoWriteException Ex)
				{
					Logger.LogWarning(Ex, "Unable to resave user {UserId}", User.Id);
				}

				if(Settings.PinnedJobIds.Count > 0)
				{
					await UpdateSettingsAsync(User.Id, AddPinnedJobIds: Settings.PinnedJobIds);
				}
			}
		}
	}
}
