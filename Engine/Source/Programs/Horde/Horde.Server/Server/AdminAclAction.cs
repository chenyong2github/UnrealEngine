// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Server.Acls;

namespace Horde.Server.Server
{
	/// <summary>
	/// ACL actions for admin operations
	/// </summary>
	public static class AdminAclAction
	{
		/// <summary>
		/// Ability to read any data from the server. Always inherited.
		/// </summary>
		public static AclAction AdminRead { get; } = new AclAction("AdminRead");

		/// <summary>
		/// Ability to write any data to the server.
		/// </summary>
		public static AclAction AdminWrite { get; } = new AclAction("AdminWrite");

		/// <summary>
		/// Ability to impersonate another user
		/// </summary>
		public static AclAction Impersonate { get; } = new AclAction("Impersonate");

		/// <summary>
		/// View estimated costs for particular operations
		/// </summary>
		public static AclAction ViewCosts { get; } = new AclAction("ViewCosts");

		/// <summary>
		/// Issue bearer token for the current user
		/// </summary>
		public static AclAction IssueBearerToken { get; } = new AclAction("IssueBearerToken");
	}
}
