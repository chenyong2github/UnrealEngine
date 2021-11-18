// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace UnrealBuildBase
{
	// Acceleration structure:
	// used to encapsulate a full set of dependencies for an msbuild project - explicit and globbed
	// These files are written to Intermediate/ScriptModules
	public class CsProjBuildRecord
	{
		// Version number making it possible to quickly invalidate written records.
		public static readonly int CurrentVersion = 4;
		public int Version { get; set; } // what value does this get if deserialized from a file with no value for this field? 

		// Path to the .csproj project file, relative to the location of the build record .json file
		public string? ProjectPath { get; set; }

		// The time that the target assembly was built (read from the file after the build)
		public DateTime TargetBuildTime { get; set; }


		// all following paths are relative to the project directory, the directory containing ProjectPath 

		// assembly (dll) location
		public string? TargetPath { get; set; }

		// Paths of referenced projects
		public HashSet<string> ProjectReferences { get; set; } = new HashSet<string>();

		// file dependencies from non-glob sources
		public HashSet<string> Dependencies { get; set; } = new HashSet<string>();

		// file dependencies from globs
		public HashSet<string> GlobbedDependencies { get; set; } = new HashSet<string>();

		public class Glob
		{
			public string? ItemType { get; set; }
			public List<string>? Include { get; set; }
			public List<string>? Exclude { get; set; }
			public List<string>? Remove { get; set; }
		}

		public List<Glob> Globs { get; set; } = new List<Glob>();
	}
}
