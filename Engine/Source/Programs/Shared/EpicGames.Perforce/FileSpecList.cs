// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Wrapper for a list of filespecs. Allows implicit conversion from string (a single entry) or list.
	/// </summary>
	public struct FileSpecList
	{
		/// <summary>
		/// Empty filespec list
		/// </summary>
		public static FileSpecList Empty { get; } = new FileSpecList(new List<string>());

		/// <summary>
		/// Matches any files in the depot
		/// </summary>
		public static FileSpecList Any { get; } = new FileSpecList(new List<string> { "//..." });

		/// <summary>
		/// The list of filespecs
		/// </summary>
		public IReadOnlyList<string> List { get; }

		/// <summary>
		/// Private constructor. Use implicit conversion operators below instead.
		/// </summary>
		/// <param name="FileSpecList">List of filespecs</param>
		private FileSpecList(IReadOnlyList<string> FileSpecList)
		{
			this.List = FileSpecList;
		}

		/// <summary>
		/// Implicit conversion operator from a list of filespecs
		/// </summary>
		/// <param name="List">The list to construct from</param>
		public static implicit operator FileSpecList(List<string> List)
		{
			return new FileSpecList(List);
		}

		/// <summary>
		/// Implicit conversion operator from an array of filespecs
		/// </summary>
		/// <param name="Array">The array to construct from</param>
		public static implicit operator FileSpecList(string[] Array)
		{
			return new FileSpecList(Array);
		}

		/// <summary>
		/// Implicit conversion operator from a single filespec
		/// </summary>
		/// <param name="FileSpec">The single filespec to construct from</param>
		public static implicit operator FileSpecList(string FileSpec)
		{
			return new FileSpecList(new string[] { FileSpec });
		}
	}
}
