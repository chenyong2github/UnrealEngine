// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Acceleration structure:
	/// used to encapsulate a full set of dependencies for an msbuild project - explicit and globbed
	/// These files are written to Intermediate/ScriptModules
	/// </summary>
	public class CsProjBuildRecord
	{
		/// <summary>
		/// Version number making it possible to quickly invalidate written records.
		/// </summary>
		public static readonly int CurrentVersion = 4;

		/// <summary>
		/// Version number for this record
		/// </summary>
		public int Version { get; set; } // what value does this get if deserialized from a file with no value for this field? 

		/// <summary>
		/// Path to the .csproj project file, relative to the location of the build record .json file
		/// </summary>
		public string? ProjectPath { get; set; }

		/// <summary>
		/// The time that the target assembly was built (read from the file after the build)
		/// </summary>
		public DateTime TargetBuildTime { get; set; }


		// all following paths are relative to the project directory, the directory containing ProjectPath 

		/// <summary>
		/// assembly (dll) location
		/// </summary>
		public string? TargetPath { get; set; }

		/// <summary>
		/// Paths of referenced projects
		/// </summary>
		public HashSet<string> ProjectReferences { get; set; } = new HashSet<string>();

		/// <summary>
		/// file dependencies from non-glob sources
		/// </summary>
		public HashSet<string> Dependencies { get; set; } = new HashSet<string>();

		/// <summary>
		/// file dependencies from globs
		/// </summary>
		public HashSet<string> GlobbedDependencies { get; set; } = new HashSet<string>();

		/// <summary>
		/// A glob pattern
		/// </summary>
		public class Glob
		{
			/// <summary>
			/// Type of item
			/// </summary>
			public string? ItemType { get; set; }

			/// <summary>
			/// Paths to include
			/// </summary>
			public List<string>? Include { get; set; }

			/// <summary>
			/// Paths to exclude
			/// </summary>
			public List<string>? Exclude { get; set; }

			/// <summary>
			/// 
			/// </summary>
			public List<string>? Remove { get; set; }
		}

		/// <summary>
		/// List of globs
		/// </summary>
		public List<Glob> Globs { get; set; } = new List<Glob>();
	}
}
