// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Subdirectory under a requested depot path
	/// </summary>
	public class DirsRecord
	{
		/// <summary>
		/// The directory name
		/// </summary>
		[PerforceTag("dir")]
		public string Dir;

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private DirsRecord()
		{
			Dir = null!;
		}

		/// <summary>
		/// Format this record for display in the debugger
		/// </summary>
		/// <returns>Summary of this revision</returns>
		public override string ToString()
		{
			return Dir;
		}
	}
}
