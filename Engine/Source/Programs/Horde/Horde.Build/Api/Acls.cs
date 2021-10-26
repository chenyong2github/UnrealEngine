// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// New claim to create
	/// </summary>
	public class CreateAclClaimRequest
	{
		/// <summary>
		/// The claim type
		/// </summary>
		[Required]
		public string Type { get; set; } = null!;

		/// <summary>
		/// The claim value
		/// </summary>
		[Required]
		public string Value { get; set; } = null!;
	}

	/// <summary>
	/// Individual entry in an ACL
	/// </summary>
	public class CreateAclEntryRequest
	{
		/// <summary>
		/// Name of the user or group
		/// </summary>
		[Required]
		public CreateAclClaimRequest Claim { get; set; } = null!;

		/// <summary>
		/// Array of actions to allow
		/// </summary>
		[Required]
		public string[] Actions { get; set; } = null!;
	}

	/// <summary>
	/// Parameters to update an ACL
	/// </summary>
	public class UpdateAclRequest
	{
		/// <summary>
		/// Entries to replace the existing ACL
		/// </summary>
		public List<CreateAclEntryRequest>? Entries { get; set; }

		/// <summary>
		/// Whether to inherit permissions from the parent ACL
		/// </summary>
		public bool? Inherit { get; set; }

		/// <summary>
		/// List of exceptions to the inherited setting
		/// </summary>
		public List<string>? Exceptions { get; set; }
	}

	/// <summary>
	/// New claim to update
	/// </summary>
	public class GetAclClaimResponse
	{
		/// <summary>
		/// The claim type
		/// </summary>
		public string Type { get; set; }

		/// <summary>
		/// The claim value
		/// </summary>
		public string Value { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Claim">The claim to construct from</param>
		public GetAclClaimResponse(AclClaim Claim)
			: this(Claim.Type, Claim.Value)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type">The claim type</param>
		/// <param name="Value">The claim value</param>
		public GetAclClaimResponse(string Type, string Value)
		{
			this.Type = Type;
			this.Value = Value;
		}
	}

	/// <summary>
	/// Individual entry in an ACL
	/// </summary>
	public class GetAclEntryResponse
	{
		/// <summary>
		/// Names of the user or group
		/// </summary>
		public GetAclClaimResponse Claim { get; set; }

		/// <summary>
		/// Array of actions to allow
		/// </summary>
		public List<string> Actions { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AclEntry">The acl entry to construct from</param>
		public GetAclEntryResponse(AclEntry AclEntry)
		{
			this.Claim = new GetAclClaimResponse(AclEntry.Claim.Type, AclEntry.Claim.Value);
			this.Actions = AclEntry.GetActionNames(AclEntry.Actions);
		}
	}

	/// <summary>
	/// Information about an ACL
	/// </summary>
	public class GetAclResponse
	{
		/// <summary>
		/// Entries to replace the existing ACL
		/// </summary>
		[Required]
		public List<GetAclEntryResponse> Entries { get; set; }

		/// <summary>
		/// Whether to inherit permissions from the parent entity
		/// </summary>
		public bool Inherit { get; set; }

		/// <summary>
		/// Exceptions from permission inheritance setting
		/// </summary>
		public List<string>? Exceptions { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Acl">The ACL to construct from</param>
		public GetAclResponse(Acl Acl)
		{
			this.Entries = Acl.Entries.ConvertAll(x => new GetAclEntryResponse(x));
			this.Inherit = Acl.Inherit;
			this.Exceptions = Acl.Exceptions?.ConvertAll(x => x.ToString());
		}
	}
}
