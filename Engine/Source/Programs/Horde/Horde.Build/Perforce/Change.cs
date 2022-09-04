// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Horde.Build.Users;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// Flags identifying content of a changelist
	/// </summary>
	[Flags]
	public enum ChangeContentFlags
	{
		/// <summary>
		/// The change contains code
		/// </summary>
		ContainsCode = 1,

		/// <summary>
		/// The change contains content
		/// </summary>
		ContainsContent = 2,
	}


	/// <summary>
	/// Summary of a file in the depot
	/// </summary>
	public class FileSummary
	{
		/// <summary>
		/// Depot path to the file
		/// </summary>
		public string DepotPath { get; set; }

		/// <summary>
		/// Whether the file exists
		/// </summary>
		public bool Exists { get; set; }

		/// <summary>
		/// Last changelist number that the file was modified
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Error about this file, if it exists
		/// </summary>
		public string? Error { get; set; }

		/// <summary>
		/// Information about a file
		/// </summary>
		/// <param name="depotPath">Depot </param>
		/// <param name="exists"></param>
		/// <param name="change"></param>
		/// <param name="error"></param>
		public FileSummary(string depotPath, bool exists, int change, string? error = null)
		{
			DepotPath = depotPath;
			Exists = exists;
			Change = change;
			Error = error;
		}
	}
}
