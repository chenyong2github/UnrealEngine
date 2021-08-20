// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.RegularExpressions;

namespace HordeServer.Utilities
{
	using P4 = Perforce.P4;

	/// <summary>
	/// Stores a mapping from one set of paths to another
	/// </summary>
	public class ViewMap
	{
		/// <summary>
		/// List of entries making up the view
		/// </summary>
		public List<ViewMapEntry> Entries { get; } = new List<ViewMapEntry>();

		/// <summary>
		/// Default constructor
		/// </summary>
		public ViewMap()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ViewMap">Peforce viewmap definition</param>
		public ViewMap(P4.ViewMap ViewMap)
		{
			foreach (P4.MapEntry Entry in ViewMap)
			{
				Entries.Add(new ViewMapEntry(Entry));
			}
		}

		/// <summary>
		/// Determines if a file is included in the view
		/// </summary>
		/// <param name="File">The file to test</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the file is included in the view</returns>
		public bool MatchFile(string File, StringComparison Comparison)
		{
			bool Included = false;
			foreach (ViewMapEntry Entry in Entries)
			{
				if (Entry.MatchFile(File, Comparison))
				{
					Included = Entry.Include;
				}
			}
			return Included;
		}

		/// <summary>
		/// Attempts to convert a source file to its target path
		/// </summary>
		/// <param name="SourceFile"></param>
		/// <param name="Comparison">The comparison type</param>
		/// <param name="TargetFile"></param>
		/// <returns></returns>
		public bool TryMapFile(string SourceFile, StringComparison Comparison, [NotNullWhen(true)] out string? TargetFile)
		{
			ViewMapEntry? MapEntry = null;
			foreach (ViewMapEntry Entry in Entries)
			{
				if (Entry.MatchFile(SourceFile, Comparison))
				{
					MapEntry = Entry;
				}
			}

			if (MapEntry != null && MapEntry.Include)
			{
				TargetFile = MapEntry.MapFile(SourceFile);
				return true;
			}
			else
			{
				TargetFile = null;
				return false;
			}
		}

		/// <summary>
		/// Tests whether the view includes any files directly under the given directory (not including any subdirectories).
		/// </summary>
		/// <param name="Dir">The directory to test; should end with a Slash.</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the view may include any files in the given directory</returns>
		public bool MayMatchAnyFilesInDirectory(string Dir, StringComparison Comparison)
		{
			bool MayInclude = false;
			foreach (ViewMapEntry Entry in Entries)
			{
				if (Entry.Include)
				{
					if (Entry.MayMatchAnyFilesInDirectory(Dir, Comparison))
					{
						MayInclude = true;
					}
				}
				else
				{
					if (Entry.MatchAllFilesInDirectory(Dir, Comparison))
					{
						MayInclude = false;
					}
				}
			}
			return MayInclude;
		}

		/// <summary>
		/// Tests whether the view may include any files in subdirectories of the given path
		/// </summary>
		/// <param name="Dir">The directory to test; should end with a Slash.</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the pattern may match files under the given directory</returns>
		public bool MayMatchAnyFilesInSubDirectory(string Dir, StringComparison Comparison)
		{
			bool MayInclude = false;
			foreach (ViewMapEntry Entry in Entries)
			{
				if (Entry.Include)
				{
					if (Entry.MayMatchAnyFilesInSubDirectory(Dir, Comparison))
					{
						MayInclude = true;
					}
				}
				else
				{
					if (Entry.MatchAllFilesInDirectory(Dir, Comparison))
					{
						MayInclude = false;
					}
				}
			}
			return MayInclude;
		}

		/// <summary>
		/// Gets the root paths from the view entries
		/// </summary>
		/// <returns></returns>
		public List<string> GetRootPaths(StringComparison Comparison)
		{
			List<string> RootPaths = new List<string>();
			foreach (ViewMapEntry Entry in Entries)
			{
				if (Entry.Include)
				{
					int LastSlashIdx = Entry.SourcePrefix.LastIndexOf('/');
					string RootPath = Entry.SourcePrefix.Substring(0, LastSlashIdx + 1);

					for (int Idx = 0; ; Idx++)
					{
						if (Idx == RootPaths.Count)
						{
							RootPaths.Add(RootPath);
							break;
						}
						else if (RootPaths[Idx].StartsWith(RootPath, Comparison))
						{
							RootPaths[Idx] = RootPath;
							break;
						}
						else if (RootPath.StartsWith(RootPaths[Idx], Comparison))
						{
							break;
						}
					}
				}
			}
			return RootPaths;
		}
	}

	/// <summary>
	/// Entry within a ViewMap
	/// </summary>
	public class ViewMapEntry
	{
		/// <summary>
		/// Whether to include files matching this pattern
		/// </summary>
		public bool Include { get; }

		/// <summary>
		/// The wildcard string - either '*' or '...'
		/// </summary>
		public string? Wildcard { get; }

		/// <summary>
		/// The source part of the pattern before the wildcard
		/// </summary>
		public string SourcePrefix { get; }

		/// <summary>
		/// The source part of the pattern after the wildcard. Perforce does not permit a slash to be in this part of the mapping.
		/// </summary>
		public string SourceSuffix { get; }

		/// <summary>
		/// The target mapping for the pattern before the wildcard
		/// </summary>
		public string TargetPrefix { get; }

		/// <summary>
		/// The target mapping for the pattern after the wildcard
		/// </summary>
		public string TargetSuffix { get; }

		/// <summary>
		/// Tests if the entry has a file wildcard ('*')
		/// </summary>
		/// <returns>True if the entry has a file wildcard</returns>
		public bool IsFileWildcard() => Wildcard != null && Wildcard.Length == 1;

		/// <summary>
		/// Tests if the entry has a path wildcard ('...')
		/// </summary>
		/// <returns>True if the entry has a path wildcard</returns>
		public bool IsPathWildcard() => Wildcard != null && Wildcard.Length == 3;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Include"></param>
		/// <param name="Source"></param>
		/// <param name="Target"></param>
		public ViewMapEntry(bool Include, string Source, string Target)
		{
			this.Include = Include;

			Match Match = Regex.Match(Source, @"^(.*)(\*|\.\.\.)(.*)$");
			if (Match.Success)
			{
				SourcePrefix = Match.Groups[1].Value;
				SourceSuffix = Match.Groups[3].Value;
				Wildcard = Match.Groups[2].Value;

				int OtherIdx = Target.IndexOf(Wildcard, StringComparison.Ordinal);
				TargetPrefix = Target.Substring(0, OtherIdx);
				TargetSuffix = Target.Substring(OtherIdx + Wildcard.Length);
			}
			else
			{
				SourcePrefix = Source;
				SourceSuffix = String.Empty;
				TargetPrefix = Target;
				TargetSuffix = String.Empty;
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Include"></param>
		/// <param name="Wildcard"></param>
		/// <param name="SourcePrefix"></param>
		/// <param name="SourceSuffix"></param>
		/// <param name="TargetPrefix"></param>
		/// <param name="TargetSuffix"></param>
		public ViewMapEntry(bool Include, string? Wildcard, string SourcePrefix, string SourceSuffix, string TargetPrefix, string TargetSuffix)
		{
			this.Include = Include;
			this.Wildcard = Wildcard;
			this.SourcePrefix = SourcePrefix;
			this.SourceSuffix = SourceSuffix;
			this.TargetPrefix = TargetPrefix;
			this.TargetSuffix = TargetSuffix;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Entry"></param>
		public ViewMapEntry(P4.MapEntry Entry)
			: this(Entry.Type == P4.MapType.Include, Entry.Left.Path, Entry.Right.Path)
		{
		}

		/// <summary>
		/// Maps a file to the target path
		/// </summary>
		/// <param name="SourceFile"></param>
		/// <returns></returns>
		public string MapFile(string SourceFile)
		{
			int Count = SourceFile.Length - SourceSuffix.Length - SourcePrefix.Length;
			return TargetPrefix + SourceFile.Substring(SourcePrefix.Length, Count) + TargetSuffix;
		}

		/// <summary>
		/// Determine if a file matches the current entry
		/// </summary>
		/// <param name="Path">Path to the file</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the path matches the entry</returns>
		public bool MatchFile(string Path, StringComparison Comparison)
		{
			if (Wildcard == null)
			{
				return Path.Equals(SourcePrefix, Comparison);
			}
			else
			{
				if (!Path.StartsWith(SourcePrefix, Comparison) || !Path.EndsWith(SourceSuffix, Comparison))
				{
					return false;
				}
				if (IsFileWildcard() && Path.IndexOf('/', SourcePrefix.Length, Path.Length - SourceSuffix.Length - SourcePrefix.Length) != -1)
				{
					return false;
				}
				return true;
			}
		}

		/// <summary>
		/// Determine if the pattern may match any files directly under the given directory
		/// </summary>
		/// <param name="Dir">The directory to test</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the pattern may match a file in this directory</returns>
		public bool MayMatchAnyFilesInDirectory(string Dir, StringComparison Comparison)
		{
			Debug.Assert(Dir.EndsWith("/", StringComparison.Ordinal));
			if (Dir.StartsWith(SourcePrefix, Comparison))
			{
				if (IsPathWildcard() || Dir.IndexOf('/', SourcePrefix.Length) == -1)
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Determine if the pattern may match any files in subdirectories of the given directory
		/// </summary>
		/// <param name="Dir">The directory to test</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the pattern may match a file in this directory</returns>
		public bool MayMatchAnyFilesInSubDirectory(string Dir, StringComparison Comparison)
		{
			Debug.Assert(Dir.EndsWith("/", StringComparison.Ordinal));
			if (Dir.StartsWith(SourcePrefix, Comparison))
			{
				if (IsPathWildcard())
				{
					return true;
				}
			}
			else
			{
				if (SourcePrefix.StartsWith(Dir, Comparison))
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Determine if the pattern matches all files in the given directory
		/// </summary>
		/// <param name="Dir">The directory to test</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the pattern may match a file in this directory</returns>
		public bool MatchAllFilesInDirectory(string Dir, StringComparison Comparison)
		{
			return Wildcard != null && Wildcard.Length == 3 && Dir.StartsWith(SourcePrefix, Comparison);
		}
	}
}
