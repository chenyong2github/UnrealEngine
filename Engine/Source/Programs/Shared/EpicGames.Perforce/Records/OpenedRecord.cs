// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Record returned by the opened command
	/// </summary>
	[DebuggerDisplay("{DepotFile}")]
	public class OpenedRecord
	{
		/// <summary>
		/// Depot path to file
		/// </summary>
		[PerforceTag("depotFile")]
		public string DepotFile { get; set; } = String.Empty;
		
		/// <summary>
		/// Client path to file
		/// </summary>
		[PerforceTag("clientFile")]
		public string ClientFile { get; set; } = String.Empty;

		/// <summary>
		/// The revision of the file 
		/// </summary>
		[PerforceTag("rev")]
		public int Revision { get; set; }

		/// <summary>
		/// The synced revision of the file (may be 'none' for adds)
		/// </summary>
		[PerforceTag("haveRev")]
		public int HaveRevision { get; set; }

		/// <summary>
		/// Open action, if opened in your workspace
		/// </summary>
		[PerforceTag("action")]
		public FileAction Action { get; set; }

		/// <summary>
		/// Change containing the open file
		/// </summary>
		[PerforceTag("change")]
		public int Change { get; set; }

		/// <summary>
		/// User with the file open
		/// </summary>
		[PerforceTag("user")]
		public string User { get; set; } = String.Empty;

		/// <summary>
		/// Client with the file open
		/// </summary>
		[PerforceTag("client")]
		public string Client { get; set; } = String.Empty;
	}
}
