// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Claim for a user
	/// </summary>
	public interface IUserClaim
	{
		/// <summary>
		/// Name of the claim
		/// </summary>
		public string Type { get; }

		/// <summary>
		/// Value of the claim
		/// </summary>
		public string Value { get; }
	}

	/// <summary>
	/// New claim document
	/// </summary>
	public class UserClaim : IUserClaim
	{
		/// <inheritdoc/>
		public string Type { get; set; }

		/// <inheritdoc/>
		public string Value { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Value"></param>
		public UserClaim(string Type, string Value)
		{
			this.Type = Type;
			this.Value = Value;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Other">Claim to construct from</param>
		public UserClaim(IUserClaim Other)
			: this(Other.Type, Other.Value)
		{
		}

		/// <summary>
		/// Constructs a UserClaim from a Claim object
		/// </summary>
		/// <param name="Claim">Claim object</param>
		/// <returns></returns>
		public static UserClaim FromClaim(Claim Claim)
		{
			return new UserClaim(Claim.Type, Claim.Value);
		}

		/// <summary>
		/// Conversion operator from NET claims
		/// </summary>
		/// <param name="Claim"></param>
		public static implicit operator UserClaim(Claim Claim) => FromClaim(Claim);
	}
}
