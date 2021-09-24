// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Contains information about the current client and server configuration
	/// </summary>
	public class InfoRecord
	{
		/// <summary>
		/// The current user according to the Perforce environment
		/// </summary>
		[PerforceTag("userName", Optional = true)]
		public string? UserName;

		/// <summary>
		/// The current client
		/// </summary>
		[PerforceTag("clientName", Optional = true)]
		public string? ClientName;

		/// <summary>
		/// The current host
		/// </summary>
		[PerforceTag("clientHost", Optional = true)]
		public string? ClientHost;

		/// <summary>
		/// Root directory for the current client
		/// </summary>
		[PerforceTag("clientRoot", Optional = true)]
		public string? ClientRoot;

		/// <summary>
		/// Selected stream in the current client
		/// </summary>
		[PerforceTag("clientStream", Optional = true)]
		public string? ClientStream;

		/// <summary>
		/// Address of the Perforce server
		/// </summary>
		[PerforceTag("serverAddress", Optional = true)]
		public string? ServerAddress;

		/// <summary>
		/// Date and time on the server
		/// </summary>
		[PerforceTag("serverDate", Optional = true)]
		public DateTimeOffset? ServerDate;

		/// <summary>
		/// Case handling setting on the server
		/// </summary>
		[PerforceTag("caseHandling", Optional = true)]
		public string? CaseHandling;

		/// <summary>
		/// How to compare paths on this server
		/// </summary>
		public StringComparer PathComparer => String.Equals(CaseHandling, "insensitive", StringComparison.OrdinalIgnoreCase) ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;

		/// <summary>
		/// How to compare paths on this server
		/// </summary>
		public StringComparison PathComparison => String.Equals(CaseHandling, "insensitive", StringComparison.OrdinalIgnoreCase) ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

		/// <summary>
		/// How to compare paths on this server
		/// </summary>
		public Utf8StringComparer Utf8PathComparer => String.Equals(CaseHandling, "insensitive", StringComparison.OrdinalIgnoreCase) ? Utf8StringComparer.OrdinalIgnoreCase : Utf8StringComparer.Ordinal;

		/// <summary>
		/// List of services provided by this server
		/// </summary>
		[PerforceTag("serverServices", Optional = true)]
		public string? Services;

		/// <summary>
		/// The server unique id
		/// </summary>
		[PerforceTag("ServerID", Optional = true)]
		public string? ServerID;
	}
}
