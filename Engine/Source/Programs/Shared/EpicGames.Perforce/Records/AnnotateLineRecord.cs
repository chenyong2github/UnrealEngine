// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace EpicGames.Perforce
{
	/// <summary>
	/// 
	/// </summary>
	public class AnnotateLineRecord
	{
		/// <summary>
		/// 
		/// </summary>
		[PerforceTag("depotFile")]
		public readonly string DepotFile;

		/// <summary>
		/// The upper changelist 
		/// </summary>
		[PerforceTag("upper")]
		public readonly int UpperRev;

		/// <summary>
		/// The lower changelist 
		/// </summary>
		[PerforceTag("lower")]
		public readonly int LowerRev;

		/// <summary>
		/// Timestamp of this modification
		/// </summary>
		[PerforceTag("time", Optional = true)]
		public readonly DateTime Time;

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private AnnotateLineRecord()
		{
			DepotFile = null!;
		}

		/// <summary>
		/// Format this record for display in the debugger
		/// </summary>
		/// <returns>Summary of this revision</returns>
		public override string ToString()
		{
			return String.Format("FileLoc {0}, from rev {1} to {2} on {3}", DepotFile, LowerRev, UpperRev, Time);
		}
	}
}
