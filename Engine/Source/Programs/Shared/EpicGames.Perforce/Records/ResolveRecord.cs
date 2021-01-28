// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Information about a resolved file
	/// </summary>
	public class ResolveRecord
	{
		/// <summary>
		/// Path to the file in the workspace
		/// </summary>
		[PerforceTag("clientFile", Optional = true)]
		public string? ClientFile;

		/// <summary>
		/// Path to the depot file that needs to be resolved
		/// </summary>
		[PerforceTag("fromFile", Optional = true)]
		public string? FromFile;

		/// <summary>
		/// Target file for the resolve
		/// </summary>
		[PerforceTag("toFile", Optional = true)]
		public string? ToFile;

		/// <summary>
		/// How the file was resolved
		/// </summary>
		[PerforceTag("how", Optional = true)]
		public string? How;

		/// <summary>
		/// Start range of changes to be resolved
		/// </summary>
		[PerforceTag("startFromRev", Optional = true)]
		public int FromRevisionStart;

		/// <summary>
		/// Ending range of changes to be resolved
		/// </summary>
		[PerforceTag("endFromRev", Optional = true)]
		public int FromRevisionEnd;

		/// <summary>
		/// The type of resolve to perform
		/// </summary>
		[PerforceTag("resolveType", Optional = true)]
		public string? ResolveType;

		/// <summary>
		/// For content resolves, the type of resolve to be performed
		/// </summary>
		[PerforceTag("contentResolveType", Optional = true)]
		public string? ContentResolveType;

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private ResolveRecord()
		{
			ClientFile = null!;
			FromFile = null!;
			ResolveType = null!;
		}

		/// <summary>
		/// Format this record for display in the debugger
		/// </summary>
		/// <returns>Summary of this revision</returns>
		public override string? ToString()
		{
			return ClientFile;
		}
	}
}
