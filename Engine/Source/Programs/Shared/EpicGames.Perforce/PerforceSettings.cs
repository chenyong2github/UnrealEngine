// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Settings for a new Perforce connection
	/// </summary>
	public class PerforceSettings
	{
		/// <summary>
		/// Server and port to connect to
		/// </summary>
		public string? ServerAndPort { get; set; }

		/// <summary>
		/// Username to log in with
		/// </summary>
		public string? User { get; set; }

		/// <summary>
		/// Password to use
		/// </summary>
		public string? Password { get; set; }

		/// <summary>
		/// Name of the client to use
		/// </summary>
		public string? Client { get; set; }

		/// <summary>
		/// The invoking application name
		/// </summary>
		public string? AppName { get; set; }

		/// <summary>
		/// The invoking application version
		/// </summary>
		public string? AppVersion { get; set; }
	}
}
