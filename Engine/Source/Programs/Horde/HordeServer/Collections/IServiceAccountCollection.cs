// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using MongoDB.Bson;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Interface for a collection of service account documents
	/// </summary>
	public interface IServiceAccountCollection
	{
		/// <summary>
		/// Adds a new service account to the collection
		/// </summary>
		/// <param name="SecretToken">Secret token used for identifying this service account</param>
		/// <param name="Claims">Dictionary of claims to assign</param>
		/// <param name="Description">Description of the account</param>
		Task<IServiceAccount> AddAsync(string SecretToken, List<string> Claims, string Description);

		/// <summary>
		/// Get service account via ID
		/// </summary>
		/// <param name="Id">The unique service account id</param>
		/// <returns>The service account</returns>
		Task<IServiceAccount?> GetAsync(ObjectId Id);
		
		/// <summary>
		/// Get service account via secret token
		/// </summary>
		/// <param name="SecretToken">Secret token to use for searching</param>
		/// <returns>The service account</returns>
		Task<IServiceAccount?> GetBySecretTokenAsync(string SecretToken);

		/// <summary>
		/// Update a service account from the collection
		/// </summary>
		/// <param name="Id">The service account</param>
		/// <param name="SecretToken">If set, secret token will be set</param>
		/// <param name="Claims">If set, claims will be set</param>
		/// <param name="Enabled">If set, enabled flag will be set</param>
		/// <param name="Description">If set, description will be set</param>
		/// <returns>Async task</returns>
		Task UpdateAsync(ObjectId Id, string? SecretToken, List<string>? Claims, bool? Enabled, string? Description);

		/// <summary>
		/// Delete a service account from the collection
		/// </summary>
		/// <param name="Id">The service account</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(ObjectId Id);
	}
}
