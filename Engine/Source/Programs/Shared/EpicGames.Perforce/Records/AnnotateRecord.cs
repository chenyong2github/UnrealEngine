// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace EpicGames.Perforce
{
	/// <summary>
	/// 
	/// </summary>
	public class AnnotateRecord
	{
		/// <summary>
		/// The revision number of this file
		/// </summary>
		[PerforceTag("rev", Optional = true)]
		public readonly int RevisionNumber;

		/// <summary>
		/// The changelist responsible for this revision of the file
		/// </summary>
		[PerforceTag("change", Optional = true)]
		public readonly int ChangeNumber;

		/// <summary>
		/// Type of the file
		/// </summary>
		[PerforceTag("type", Optional = true)]
		public readonly string Type;

		/// <summary>
		/// The upper changelist 
		/// </summary>
		[PerforceTag("upper", Optional = true)]
		public readonly string UpperCL;

		/// <summary>
		/// The lower changelist 
		/// </summary>
		[PerforceTag("lower", Optional = true)]
		public readonly string LowerCL;

		/// <summary>
		/// Author of the changelist
		/// </summary>
		[PerforceTag("user", Optional = true)]
		public readonly string UserName;

		/// <summary>
		/// Timestamp of this modification
		/// </summary>
		[PerforceTag("time", Optional = true)]
		public readonly DateTime Time;

		/// <summary>
		/// Client that submitted this changelist
		/// </summary>
		[PerforceTag("client", Optional = true)]
		public readonly string ClientName;

		/// <summary>
		/// The actual line
		/// </summary>
		[PerforceTag("data", Optional = true)]
		public readonly string Data;
		
		/// <summary>
		/// 
		/// </summary>
		[PerforceRecordList]
		public List<AnnotateLineRecord> LineRecord = new List<AnnotateLineRecord>();

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private AnnotateRecord()
		{
			Type = null!;
			UserName = null!;
			ClientName = null!;
			Data = null!;
			UpperCL = null!;
			LowerCL = null!;
		}

		/// <summary>
		/// Format this record for display in the debugger
		/// </summary>
		/// <returns>Summary of this revision</returns>
		public override string ToString()
		{
			if (Data == null) // this is the header
			{
				return String.Format("Data for revision {0}, CL {1}", RevisionNumber, ChangeNumber);
			}
			else
			{
				return String.Format("CL {0} by {1}, on {2}, line = {3}", LowerCL, UserName, Time, Data);
			}
		}
	}
}
