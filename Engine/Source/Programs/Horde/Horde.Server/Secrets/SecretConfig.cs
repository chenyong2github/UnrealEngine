// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Security.Claims;
using System.Text.Json.Serialization;
using Horde.Server.Acls;
using Horde.Server.Server;

namespace Horde.Server.Secrets
{
	/// <summary>
	/// Configuration for a secret value
	/// </summary>
	public class SecretConfig
	{
		[JsonIgnore]
		internal GlobalConfig GlobalConfig { get; private set; } = null!;

		/// <summary>
		/// Identifier for this secret
		/// </summary>
		public SecretId Id { get; set; }

		/// <summary>
		/// Key/value pairs associated with this secret
		/// </summary>
		public Dictionary<string, string> Data { get; set; } = new Dictionary<string, string>();

		/// <summary>
		/// Defines access to this particular secret
		/// </summary>
		public AclConfig? Acl { get; set; }

		/// <summary>
		/// Called after the config has been read
		/// </summary>
		/// <param name="globalConfig">Parent GlobalConfig object</param>
		public void PostLoad(GlobalConfig globalConfig)
		{
			GlobalConfig = globalConfig;
		}

		/// <summary>
		/// Authorizes a user to perform a given action
		/// </summary>
		/// <param name="action">The action being performed</param>
		/// <param name="user">The principal to validate</param>
		public bool Authorize(AclAction action, ClaimsPrincipal user)
		{
			return Acl?.Authorize(action, user) ?? GlobalConfig.Authorize(action, user);
		}
	}
}
