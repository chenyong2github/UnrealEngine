// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of service account documents
	/// </summary>
	public class ServiceAccountCollection : IServiceAccountCollection
	{
		/// <summary>
		/// Concrete implementation of IServiceAccount
		/// </summary>
		class ServiceAccountDocument : IServiceAccount
		{
			public const string ClaimSeparator = "###";
			
			[BsonRequired, BsonId]
			public ObjectId Id { get; set; }

			[BsonRequired]
			public string SecretToken { get; set; } = "empty";

			[BsonRequired]
			public List<string> Claims { get; set; } = new List<string>();
			
			public bool Enabled { get; set; }
			public string Description { get; set; } = "empty";

			[BsonConstructor]
			private ServiceAccountDocument()
			{
			}

			public ServiceAccountDocument(ObjectId Id, string SecretToken, List<string> Claims, bool Enabled, string Description)
			{
				this.Id = Id;
				this.SecretToken = SecretToken;
				this.Claims = Claims;
				this.Enabled = Enabled;
				this.Description = Description;
			}
			
			/// <inheritdoc/>
			public void AddClaim(string Type, string Value)
			{
				Claims.Add(Type + ClaimSeparator + Value);
			}

			/// <inheritdoc/>
			public IReadOnlyList<(string Type, string Value)> GetClaims()
			{
				return Claims.Select(x =>
				{
					string[] Split = x.Split(ClaimSeparator);
					return (Split[0], Split[1]);
				}).ToList();
			}

			protected bool Equals(ServiceAccountDocument other)
			{
				bool AreClaimsEqual = !Claims.Except(other.Claims).Any();

                return Id.Equals(other.Id) && SecretToken == other.SecretToken && AreClaimsEqual && Enabled == other.Enabled && Description == other.Description;
			}

			public override bool Equals(object? obj)
			{
				if (ReferenceEquals(null, obj)) return false;
				if (ReferenceEquals(this, obj)) return true;
				if (obj.GetType() != this.GetType()) return false;
				return Equals((ServiceAccountDocument) obj);
			}

			public override int GetHashCode()
			{
				return HashCode.Combine(Id, SecretToken, Claims, Enabled, Description);
			}
		}

		/// <summary>
		/// Collection of session documents
		/// </summary>
		private readonly IMongoCollection<ServiceAccountDocument> ServiceAccounts;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service</param>
		public ServiceAccountCollection(DatabaseService DatabaseService)
		{
			ServiceAccounts = DatabaseService.GetCollection<ServiceAccountDocument>("ServiceAccounts");

			if (!DatabaseService.ReadOnlyMode)
			{
				ServiceAccounts.Indexes.CreateOne(new CreateIndexModel<ServiceAccountDocument>(Builders<ServiceAccountDocument>.IndexKeys.Ascending(x => x.SecretToken)));
			}
		}

		/// <inheritdoc/>
		public async Task<IServiceAccount> AddAsync(string SecretToken, List<string> Claims, string Description)
		{
			ServiceAccountDocument NewSession = new ServiceAccountDocument(ObjectId.GenerateNewId(), SecretToken, Claims, true, Description);
			await ServiceAccounts.InsertOneAsync(NewSession);
			return NewSession;
		}

		/// <inheritdoc/>
		public async Task<IServiceAccount?> GetAsync(ObjectId Id)
		{
			return await ServiceAccounts.Find(x => x.Id == Id).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IServiceAccount?> GetBySecretTokenAsync(string SecretToken)
		{
			return await ServiceAccounts.Find(x => x.SecretToken == SecretToken).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public Task UpdateAsync(ObjectId Id, string? SecretToken, List<string>? Claims, bool? Enabled, string? Description)
		{
			UpdateDefinitionBuilder<ServiceAccountDocument> Update = Builders<ServiceAccountDocument>.Update;
			List<UpdateDefinition<ServiceAccountDocument>> Updates = new List<UpdateDefinition<ServiceAccountDocument>>();
			
			if (SecretToken != null) Updates.Add(Update.Set(x => x.SecretToken, SecretToken));
			if (Claims != null) Updates.Add(Update.Set(x => x.Claims, Claims));
			if (Enabled != null) Updates.Add(Update.Set(x => x.Enabled, Enabled));
			if (Description != null) Updates.Add(Update.Set(x => x.Description, Description));

			return ServiceAccounts.FindOneAndUpdateAsync(x => x.Id == Id, Update.Combine(Updates));
		}

		/// <inheritdoc/>
		public Task DeleteAsync(ObjectId SessionId)
		{
			return ServiceAccounts.DeleteOneAsync(x => x.Id == SessionId);
		}
	}
}
