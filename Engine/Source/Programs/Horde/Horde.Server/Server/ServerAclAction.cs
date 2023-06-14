// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Server.Acls;

namespace Horde.Server.Server
{
	/// <summary>
	/// General server ACL actions
	/// </summary>
	public static class ServerAclAction
	{
		/// <summary>
		/// Ability to impersonate another user
		/// </summary>
		public static AclAction Impersonate { get; } = new AclAction("Impersonate");

		/// <summary>
		/// View estimated costs for particular operations
		/// </summary>
		public static AclAction ViewCosts { get; } = new AclAction("ViewCosts");

		/// <summary>
		/// View estimated costs for particular operations
		/// </summary>
		public static AclAction ViewConfig { get; } = new AclAction("ViewConfig");

		/// <summary>
		/// Issue bearer token for the current user
		/// </summary>
		public static AclAction IssueBearerToken { get; } = new AclAction("IssueBearerToken");
	}
}
