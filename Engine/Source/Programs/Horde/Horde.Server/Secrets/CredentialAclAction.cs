// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Server.Acls;

namespace Horde.Server.Secrets
{
	/// <summary>
	/// ACL actions for credentials
	/// </summary>
	public static class CredentialAclAction
	{
		/// <summary>
		/// Create a new credential
		/// </summary>
		public static AclAction CreateCredential { get; } = new AclAction("CreateCredential");

		/// <summary>
		/// Delete a credential
		/// </summary>
		public static AclAction DeleteCredential { get; } = new AclAction("DeleteCredential");

		/// <summary>
		/// Modify an existing credential
		/// </summary>
		public static AclAction UpdateCredential { get; } = new AclAction("UpdateCredential");

		/// <summary>
		/// Enumerates all the available credentials
		/// </summary>
		public static AclAction ListCredentials { get; } = new AclAction("ListCredentials");

		/// <summary>
		/// View a credential
		/// </summary>
		public static AclAction ViewCredential { get; } = new AclAction("ViewCredential");
	}
}
