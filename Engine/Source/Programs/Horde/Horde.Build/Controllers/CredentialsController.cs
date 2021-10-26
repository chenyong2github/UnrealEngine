// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/credentials endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class CredentialsController : ControllerBase
	{
		/// <summary>
		/// Singleton instance of the ACL service
		/// </summary>
		private readonly AclService AclService;

		/// <summary>
		/// Singleton instance of the credential service
		/// </summary>
		private readonly CredentialService CredentialService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclService">The ACL service</param>
		/// <param name="CredentialService">The credential service</param>
		public CredentialsController(AclService AclService, CredentialService CredentialService)
		{
			this.AclService = AclService;
			this.CredentialService = CredentialService;
		}

		/// <summary>
		/// Creates a new credential
		/// </summary>
		/// <param name="Create">Parameters for the new credential.</param>
		/// <returns>Http result code</returns>
		[HttpPost]
		[Route("/api/v1/credentials")]
		public async Task<ActionResult<CreateCredentialResponse>> CreateCredentialAsync([FromBody] CreateCredentialRequest Create)
		{
			if(!await AclService.AuthorizeAsync(AclAction.CreateCredential, User))
			{
				return Forbid();
			}

			Credential NewCredential = await CredentialService.CreateCredentialAsync(Create.Name, Create.Properties);
			return new CreateCredentialResponse(NewCredential.Id.ToString());
		}

		/// <summary>
		/// Query all the credentials
		/// </summary>
		/// <param name="Name">Id of the credential to get information about</param>
		/// <param name="Filter">Filter for the properties to return</param>
		/// <returns>Information about all the credentials</returns>
		[HttpGet]
		[Route("/api/v1/credentials")]
		[ProducesResponseType(typeof(List<GetCredentialResponse>), 200)]
		public async Task<ActionResult<object>> FindCredentialAsync([FromQuery] string? Name = null, [FromQuery] PropertyFilter? Filter = null)
		{
			if (!await AclService.AuthorizeAsync(AclAction.ListCredentials, User))
			{
				return Forbid();
			}

			List<Credential> Credentials = await CredentialService.FindCredentialsAsync(Name);
			GlobalPermissionsCache Cache = new GlobalPermissionsCache();

			List<object> Responses = new List<object>();
			foreach (Credential Credential in Credentials)
			{
				if (await CredentialService.AuthorizeAsync(Credential, AclAction.ViewCredential, User, Cache))
				{
					bool bIncludeAcl = await CredentialService.AuthorizeAsync(Credential, AclAction.ViewPermissions, User, Cache);
					Responses.Add(new GetCredentialResponse(Credential, bIncludeAcl).ApplyFilter(Filter));
				}
			}
			return Responses;
		}

		/// <summary>
		/// Retrieve information about a specific credential
		/// </summary>
		/// <param name="CredentialId">Id of the credential to get information about</param>
		/// <param name="Filter">Filter for properties to return</param>
		/// <returns>Information about the requested credential</returns>
		[HttpGet]
		[Route("/api/v1/credentials/{CredentialId}")]
		[ProducesResponseType(typeof(GetCredentialResponse), 200)]
		public async Task<ActionResult<object>> GetCredentialAsync(string CredentialId, [FromQuery] PropertyFilter? Filter = null)
		{
			ObjectId ProjectIdValue = CredentialId.ToObjectId();

			Credential? Credential = await CredentialService.GetCredentialAsync(ProjectIdValue);
			if (Credential == null)
			{
				return NotFound();
			}

			GlobalPermissionsCache Cache = new GlobalPermissionsCache();
			if (!await CredentialService.AuthorizeAsync(Credential, AclAction.ViewCredential, User, Cache))
			{
				return Forbid();
			}

			bool bIncludeAcl = await CredentialService.AuthorizeAsync(Credential, AclAction.ViewPermissions, User, Cache);
			return new GetCredentialResponse(Credential, bIncludeAcl).ApplyFilter(Filter);
		}

		/// <summary>
		/// Update a credential's properties.
		/// </summary>
		/// <param name="CredentialId">Id of the credential to update</param>
		/// <param name="Update">Items on the credential to update</param>
		/// <returns>Http result code</returns>
		[HttpPut]
		[Route("/api/v1/credentials/{CredentialId}")]
		public async Task<ActionResult> UpdateCredentialAsync(string CredentialId, [FromBody] UpdateCredentialRequest Update)
		{
			ObjectId CredentialIdValue = CredentialId.ToObjectId();

			Credential? Credential = await CredentialService.GetCredentialAsync(CredentialIdValue);
			if(Credential == null)
			{
				return NotFound();
			}

			GlobalPermissionsCache Cache = new GlobalPermissionsCache();
			if (!await CredentialService.AuthorizeAsync(Credential, AclAction.UpdateCredential, User, Cache))
			{
				return Forbid();
			}
			if (Update.Acl != null && !await CredentialService.AuthorizeAsync(Credential, AclAction.ChangePermissions, User, Cache))
			{
				return Forbid();
			}

			await CredentialService.UpdateCredentialAsync(CredentialIdValue, Update.Name, Update.Properties, Acl.Merge(Credential.Acl, Update.Acl));
			return new OkResult();
		}

		/// <summary>
		/// Delete a credential
		/// </summary>
		/// <param name="CredentialId">Id of the credential to delete</param>
		/// <returns>Http result code</returns>
		[HttpDelete]
		[Route("/api/v1/credentials/{CredentialId}")]
		public async Task<ActionResult> DeleteCredentialAsync(string CredentialId)
		{
			ObjectId CredentialIdValue = CredentialId.ToObjectId();

			Credential? Credential = await CredentialService.GetCredentialAsync(CredentialIdValue);
			if (Credential == null)
			{
				return NotFound();
			}
			if (!await CredentialService.AuthorizeAsync(Credential, AclAction.DeleteCredential, User, null))
			{
				return Forbid();
			}
			if (!await CredentialService.DeleteCredentialAsync(CredentialIdValue))
			{
				return NotFound();
			}
			return new OkResult();
		}
	}
}
