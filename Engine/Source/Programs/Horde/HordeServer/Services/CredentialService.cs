// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Service for accessing and modifying credentials
	/// </summary>
	public class CredentialService
	{
		/// <summary>
		/// The ACL service instance
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Collection of credential documents
		/// </summary>
		IMongoCollection<Credential> Credentials; 

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service instance</param>
		/// <param name="DatabaseService">The database service instance</param>
		public CredentialService(AclService AclService, DatabaseService DatabaseService)
		{
			this.AclService = AclService;

			Credentials = DatabaseService.Credentials;

			if (!DatabaseService.ReadOnlyMode)
			{
				Credentials.Indexes.CreateOne(new CreateIndexModel<Credential>(Builders<Credential>.IndexKeys.Ascending(x => x.NormalizedName), new CreateIndexOptions { Unique = true }));
			}
		}

		/// <summary>
		/// Creates a new credential
		/// </summary>
		/// <param name="Name">Name of the new credential</param>
		/// <param name="Properties">Properties for the new credential</param>
		/// <returns>The new credential document</returns>
		public async Task<Credential> CreateCredentialAsync(string Name, Dictionary<string, string>? Properties)
		{
			Credential NewCredential = new Credential(Name);
			if (Properties != null)
			{
				NewCredential.Properties = new Dictionary<string, string>(Properties, NewCredential.Properties.Comparer);
			}

			await Credentials.InsertOneAsync(NewCredential);
			return NewCredential;
		}

		/// <summary>
		/// Updates an existing credential
		/// </summary>
		/// <param name="Id">Unique id of the credential</param>
		/// <param name="NewName">The new name for the credential</param>
		/// <param name="NewProperties">Properties on the credential to update. Any properties with a value of null will be removed.</param>
		/// <param name="NewAcl">The new ACL for the credential</param>
		/// <returns>Async task object</returns>
		public async Task UpdateCredentialAsync(ObjectId Id, string? NewName, Dictionary<string, string?>? NewProperties, Acl? NewAcl)
		{
			UpdateDefinitionBuilder<Credential> UpdateBuilder = Builders<Credential>.Update;

			List<UpdateDefinition<Credential>> Updates = new List<UpdateDefinition<Credential>>();
			if (NewName != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Name, NewName));
				Updates.Add(UpdateBuilder.Set(x => x.NormalizedName, Credential.GetNormalizedName(NewName)));
			}
			if (NewProperties != null)
			{
				foreach (KeyValuePair<string, string?> Pair in NewProperties)
				{
					if (Pair.Value == null)
					{
						Updates.Add(UpdateBuilder.Unset(x => x.Properties[Pair.Key]));
					}
					else
					{
						Updates.Add(UpdateBuilder.Set(x => x.Properties[Pair.Key], Pair.Value));
					}
				}
			}
			if (NewAcl != null)
			{
				Updates.Add(Acl.CreateUpdate<Credential>(x => x.Acl!, NewAcl));
			}

			if (Updates.Count > 0)
			{
				await Credentials.FindOneAndUpdateAsync<Credential>(x => x.Id == Id, UpdateBuilder.Combine(Updates));
			}
		}

		/// <summary>
		/// Gets all the available credentials
		/// </summary>
		/// <returns>List of project documents</returns>
		public Task<List<Credential>> FindCredentialsAsync(string? Name)
		{
			FilterDefinitionBuilder<Credential> FilterBuilder = Builders<Credential>.Filter;

			FilterDefinition<Credential> Filter = FilterDefinition<Credential>.Empty;
			if (Name != null)
			{
				Filter &= FilterBuilder.Eq(x => x.NormalizedName, Credential.GetNormalizedName(Name));
			}

			return Credentials.Find(Filter).ToListAsync();
		}

		/// <summary>
		/// Gets a credential by ID
		/// </summary>
		/// <param name="Id">Unique id of the credential</param>
		/// <returns>The credential document</returns>
		public async Task<Credential?> GetCredentialAsync(ObjectId Id)
		{
			return await Credentials.Find<Credential>(x => x.Id == Id).FirstOrDefaultAsync();
		}

		/// <summary>
		/// Gets a credential by name
		/// </summary>
		/// <param name="Name">Name of the credential</param>
		/// <returns>The credential document</returns>
		public async Task<Credential?> GetCredentialAsync(string Name)
		{
			string NormalizedName = Credential.GetNormalizedName(Name);
			return await Credentials.Find<Credential>(x => x.NormalizedName == NormalizedName).FirstOrDefaultAsync();
		}

		/// <summary>
		/// Deletes a credential by id
		/// </summary>
		/// <param name="Id">Unique id of the credential</param>
		/// <returns>True if the credential was deleted</returns>
		public async Task<bool> DeleteCredentialAsync(ObjectId Id)
		{
			DeleteResult Result = await Credentials.DeleteOneAsync<Credential>(x => x.Id == Id);
			return Result.DeletedCount > 0;
		}

		/// <summary>
		/// Determines if the user is authorized to perform an action on a particular credential
		/// </summary>
		/// <param name="Credential">The credential to check</param>
		/// <param name="Action">The action being performed</param>
		/// <param name="User">The principal to authorize</param>
		/// <param name="Cache">Permissions cache</param>
		/// <returns>True if the action is authorized</returns>
		public Task<bool> AuthorizeAsync(Credential Credential, AclAction Action, ClaimsPrincipal User, GlobalPermissionsCache? Cache)
		{
			bool? Result = Credential.Acl?.Authorize(Action, User);
			if (Result == null)
			{
				return AclService.AuthorizeAsync(Action, User, Cache);
			}
			else
			{
				return Task.FromResult(Result.Value);
			}
		}
	}
}
