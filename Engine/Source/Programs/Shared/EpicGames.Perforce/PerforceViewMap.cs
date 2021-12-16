// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Stores a mapping from one set of paths to another
	/// </summary>
	public class PerforceViewMap
	{
		/// <summary>
		/// List of entries making up the view
		/// </summary>
		public List<PerforceViewMapEntry> Entries { get; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public PerforceViewMap()
		{
			Entries = new List<PerforceViewMapEntry>();
		}

		/// <summary>
		/// Construct from an existing set of entries
		/// </summary>
		/// <param name="Entries"></param>
		public PerforceViewMap(IEnumerable<PerforceViewMapEntry> Entries)
		{
			this.Entries = Entries.ToList();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Other"></param>
		public PerforceViewMap(PerforceViewMap Other)
		{
			Entries = new List<PerforceViewMapEntry>(Other.Entries);
		}

		/// <summary>
		/// Construct a view map from a set of entries
		/// </summary>
		/// <param name="Entries"></param>
		public static PerforceViewMap Parse(IEnumerable<string> Entries)
		{
			return new PerforceViewMap(Entries.Select(x => PerforceViewMapEntry.Parse(x)));
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
			foreach (PerforceViewMapEntry Entry in Entries)
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
		public bool TryMapFile(string SourceFile, StringComparison Comparison, out string TargetFile)
		{
			PerforceViewMapEntry? MapEntry = null;
			foreach (PerforceViewMapEntry Entry in Entries)
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
				TargetFile = String.Empty;
				return false;
			}
		}

		/// <summary>
		/// Gets the root paths from the view entries
		/// </summary>
		/// <returns></returns>
		public List<string> GetRootPaths(StringComparison Comparison)
		{
			List<string> RootPaths = new List<string>();
			foreach (PerforceViewMapEntry Entry in Entries)
			{
				if (Entry.Include)
				{
					int LastSlashIdx = Entry.SourcePrefix.LastIndexOf('/');
					ReadOnlySpan<char> RootPath = Entry.SourcePrefix.AsSpan(0, LastSlashIdx + 1);

					for (int Idx = 0; ; Idx++)
					{
						if (Idx == RootPaths.Count)
						{
							RootPaths.Add(RootPath.ToString());
							break;
						}
						else if (RootPaths[Idx].AsSpan().StartsWith(RootPath, Comparison))
						{
							RootPaths[Idx] = RootPath.ToString();
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
	public class PerforceViewMapEntry
	{
		/// <summary>
		/// Whether to include files matching this pattern
		/// </summary>
		public bool Include { get; }

		/// <summary>
		/// The wildcard string - either '*' or '...'
		/// </summary>
		public string Wildcard { get; }

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
		/// The full source pattern
		/// </summary>
		public string Source => $"{SourcePrefix}{Wildcard}{SourceSuffix}";

		/// <summary>
		/// The full target pattern
		/// </summary>
		public string Target => $"{TargetPrefix}{Wildcard}{TargetSuffix}";

		/// <summary>
		/// Tests if the entry has a file wildcard ('*')
		/// </summary>
		/// <returns>True if the entry has a file wildcard</returns>
		public bool IsFileWildcard() => Wildcard.Length == 1;

		/// <summary>
		/// Tests if the entry has a path wildcard ('...')
		/// </summary>
		/// <returns>True if the entry has a path wildcard</returns>
		public bool IsPathWildcard() => Wildcard.Length == 3;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Other"></param>
		public PerforceViewMapEntry(PerforceViewMapEntry Other)
			: this(Other.Include, Other.Wildcard, Other.SourcePrefix, Other.SourceSuffix, Other.TargetPrefix, Other.TargetSuffix)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Include"></param>
		/// <param name="Source"></param>
		/// <param name="Target"></param>
		public PerforceViewMapEntry(bool Include, string Source, string Target)
		{
			this.Include = Include;

			Match Match = Regex.Match(Source, @"^(.*)(\*|\.\.\.|%%1)(.*)$");
			if (Match.Success)
			{
				string WildcardStr = Match.Groups[2].Value;

				SourcePrefix = Match.Groups[1].Value;
				SourceSuffix = Match.Groups[3].Value;
				Wildcard = Match.Groups[2].Value;

				int OtherIdx = Target.IndexOf(WildcardStr, StringComparison.Ordinal);
				TargetPrefix = Target.Substring(0, OtherIdx);
				TargetSuffix = Target.Substring(OtherIdx + Wildcard.Length);

				if (WildcardStr.Equals("%%1", StringComparison.Ordinal))
				{
					Wildcard = "*";
				}
			}
			else
			{
				SourcePrefix = Source;
				SourceSuffix = String.Empty;
				TargetPrefix = Target;
				TargetSuffix = String.Empty;
				Wildcard = String.Empty;
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
		public PerforceViewMapEntry(bool Include, string Wildcard, string SourcePrefix, string SourceSuffix, string TargetPrefix, string TargetSuffix)
		{
			this.Include = Include;
			this.Wildcard = Wildcard;
			this.SourcePrefix = SourcePrefix;
			this.SourceSuffix = SourceSuffix;
			this.TargetPrefix = TargetPrefix;
			this.TargetSuffix = TargetSuffix;
		}

		/// <summary>
		/// Parse a view map entry from a string, as returned by spec documents
		/// </summary>
		/// <param name="Entry"></param>
		/// <returns></returns>
		public static PerforceViewMapEntry Parse(string Entry)
		{
			Match Match = Regex.Match(Entry, @"^\s*(-?)\s*([^ ]+)\s+([^ ]+)\s*$");
			if(!Match.Success)
			{
				throw new PerforceException($"Unable to parse view map entry: {Entry}");
			}
			return new PerforceViewMapEntry(Match.Groups[1].Length == 0, Match.Groups[2].Value, Match.Groups[3].Value);
		}

		/// <summary>
		/// Maps a file to the target path
		/// </summary>
		/// <param name="SourceFile"></param>
		/// <returns></returns>
		public string MapFile(string SourceFile)
		{
			int Count = SourceFile.Length - SourceSuffix.Length - SourcePrefix.Length;
			return String.Concat(TargetPrefix, SourceFile.AsSpan(SourcePrefix.Length, Count), TargetSuffix);
		}

		/// <summary>
		/// Determine if a file matches the current entry
		/// </summary>
		/// <param name="Path">Path to the file</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the path matches the entry</returns>
		public bool MatchFile(string Path, StringComparison Comparison)
		{
			if (Wildcard.Length == 0)
			{
				return String.Equals(Path, SourcePrefix, Comparison);
			}
			else
			{
				if (!Path.StartsWith(SourcePrefix, Comparison) || !Path.EndsWith(SourceSuffix, Comparison))
				{
					return false;
				}
				if (IsFileWildcard() && Path.AsSpan(SourcePrefix.Length, Path.Length - SourceSuffix.Length - SourcePrefix.Length).IndexOf('/') != -1)
				{
					return false;
				}
				return true;
			}
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			StringBuilder Builder = new StringBuilder();
			if (!Include)
			{
				Builder.Append('-');
			}
			Builder.Append($"{SourcePrefix}{Wildcard}{SourceSuffix} {TargetPrefix}{Wildcard}{TargetSuffix}");
			return Builder.ToString();
		}
	}
}
