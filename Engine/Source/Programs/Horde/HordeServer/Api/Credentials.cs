// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Parameters to create a new credential
	/// </summary>
	public class CreateCredentialRequest
	{
		/// <summary>
		/// Name for the new credential
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Properties for the new credential
		/// </summary>
		public Dictionary<string, string>? Properties { get; set; }
	}

	/// <summary>
	/// Response from creating a new credential
	/// </summary>
	public class CreateCredentialResponse
	{
		/// <summary>
		/// Unique id for the new credential
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id for the new credential</param>
		public CreateCredentialResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Parameters to update a credential
	/// </summary>
	public class UpdateCredentialRequest
	{
		/// <summary>
		/// Optional new name for the credential
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Properties to update for the credential. Properties set to null will be removed.
		/// </summary>
		public Dictionary<string, string?>? Properties { get; set; }

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Response describing a credential
	/// </summary>
	public class GetCredentialResponse
	{
		/// <summary>
		/// Unique id of the credential
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the credential
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Properties for the credential
		/// </summary>
		public Dictionary<string, string> Properties { get; set; }

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public GetAclResponse? Acl { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Credential">The credential to construct from</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		public GetCredentialResponse(Credential Credential, bool bIncludeAcl)
		{
			this.Id = Credential.Id.ToString();
			this.Name = Credential.Name;
			this.Properties = Credential.Properties;
			this.Acl = (bIncludeAcl && Credential.Acl != null)? new GetAclResponse(Credential.Acl) : null;
		}
	}
}
