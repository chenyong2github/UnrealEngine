// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Record output by the filelog command
	/// </summary>
	public class FileLogRecord
	{
		/// <summary>
		/// Path to the file in the depot
		/// </summary>
		[PerforceTag("depotFile")]
		public string DepotPath;

		/// <summary>
		/// Revisions of this file
		/// </summary>
		[PerforceRecordList]
		public List<RevisionRecord> Revisions = new List<RevisionRecord>();

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private FileLogRecord()
		{
			DepotPath = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DepotPath">Path to the file in the depot</param>
		/// <param name="Revisions">Revisions of this file</param>
		public FileLogRecord(string DepotPath, List<RevisionRecord> Revisions)
		{
			this.DepotPath = DepotPath;
			this.Revisions = Revisions;
		}

		/// <summary>
		/// Format this record for display in the debugger
		/// </summary>
		/// <returns>Summary of this revision</returns>
		public override string ToString()
		{
			return DepotPath;
		}
	}
}
