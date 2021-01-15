// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Perforce
{
	/// <summary>
	/// An instance of PerforceConnection which is strongly typed as having a valid client name
	/// </summary>
	public class PerforceClientConnection : PerforceConnection
	{
		/// <summary>
		/// Accessor for the client name.
		/// </summary>
		public new string ClientName
		{
			get { return base.ClientName!; }
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="ClientName">The client name</param>
		public PerforceClientConnection(PerforceConnection Perforce, string ClientName)
			: base(Perforce.ServerAndPort, Perforce.UserName, ClientName, Perforce.Logger)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ServerAndPort">The server and port address</param>
		/// <param name="UserName">Username to connect with</param>
		/// <param name="ClientName">The client name to use</param>
		/// <param name="Logger">Logging device</param>
		public PerforceClientConnection(string? ServerAndPort, string? UserName, string ClientName, ILogger Logger) 
			: base(ServerAndPort, UserName, ClientName, Logger)
		{
		}

		/// <summary>
		/// Remove the client parameter for the connection
		/// </summary>
		/// <returns>Bare connection to the server</returns>
		public PerforceConnection WithoutClient()
		{
			return new PerforceConnection(this) { ClientName = null };
		}
	}
}

