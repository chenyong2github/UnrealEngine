// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text;
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
		public List<ViewMapEntry> Entries { get; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public ViewMap()
		{
			Entries = new List<ViewMapEntry>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Other"></param>
		public ViewMap(ViewMap Other)
		{
			Entries = new List<ViewMapEntry>(Other.Entries);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ViewMap">Peforce viewmap definition</param>
		public ViewMap(P4.ViewMap ViewMap)
			: this()
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
		public bool MatchFile(Utf8String File, Utf8StringComparer Comparison)
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
		public bool TryMapFile(Utf8String SourceFile, Utf8StringComparer Comparison, out Utf8String TargetFile)
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
				TargetFile = Utf8String.Empty;
				return false;
			}
		}

		/// <summary>
		/// Gets the root paths from the view entries
		/// </summary>
		/// <returns></returns>
		public List<Utf8String> GetRootPaths(Utf8StringComparer Comparison)
		{
			List<Utf8String> RootPaths = new List<Utf8String>();
			foreach (ViewMapEntry Entry in Entries)
			{
				if (Entry.Include)
				{
					int LastSlashIdx = Entry.SourcePrefix.LastIndexOf('/');
					Utf8String RootPath = Entry.SourcePrefix.Slice(0, LastSlashIdx + 1);

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
		public Utf8String Wildcard { get; }

		/// <summary>
		/// The source part of the pattern before the wildcard
		/// </summary>
		public Utf8String SourcePrefix { get; }

		/// <summary>
		/// The source part of the pattern after the wildcard. Perforce does not permit a slash to be in this part of the mapping.
		/// </summary>
		public Utf8String SourceSuffix { get; }

		/// <summary>
		/// The target mapping for the pattern before the wildcard
		/// </summary>
		public Utf8String TargetPrefix { get; }

		/// <summary>
		/// The target mapping for the pattern after the wildcard
		/// </summary>
		public Utf8String TargetSuffix { get; }

		/// <summary>
		/// The full source pattern
		/// </summary>
		public Utf8String Source => $"{SourcePrefix}{Wildcard}{SourceSuffix}";

		/// <summary>
		/// The full target pattern
		/// </summary>
		public Utf8String Target => $"{TargetPrefix}{Wildcard}{TargetSuffix}";

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
		public ViewMapEntry(ViewMapEntry Other)
			: this(Other.Include, Other.Wildcard, Other.SourcePrefix, Other.SourceSuffix, Other.TargetPrefix, Other.TargetSuffix)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Include"></param>
		/// <param name="Source"></param>
		/// <param name="Target"></param>
		public ViewMapEntry(bool Include, string Source, string Target)
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
				SourceSuffix = Utf8String.Empty;
				TargetPrefix = Target;
				TargetSuffix = Utf8String.Empty;
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
		public ViewMapEntry(bool Include, Utf8String Wildcard, Utf8String SourcePrefix, Utf8String SourceSuffix, Utf8String TargetPrefix, Utf8String TargetSuffix)
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
		public Utf8String MapFile(Utf8String SourceFile)
		{
			int Count = SourceFile.Length - SourceSuffix.Length - SourcePrefix.Length;
			return TargetPrefix + SourceFile.Slice(SourcePrefix.Length, Count) + TargetSuffix;
		}

		/// <summary>
		/// Determine if a file matches the current entry
		/// </summary>
		/// <param name="Path">Path to the file</param>
		/// <param name="Comparison">The comparison type</param>
		/// <returns>True if the path matches the entry</returns>
		public bool MatchFile(Utf8String Path, Utf8StringComparer Comparison)
		{
			if (Wildcard.Length == 0)
			{
				return Comparison.Compare(Path, SourcePrefix) == 0;
			}
			else
			{
				if (!Path.StartsWith(SourcePrefix, Comparison) || !Path.EndsWith(SourceSuffix, Comparison))
				{
					return false;
				}
				if (IsFileWildcard() && Path.Slice(SourcePrefix.Length, Path.Length - SourceSuffix.Length - SourcePrefix.Length).IndexOf('/') != -1)
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
				Builder.Append("-");
			}
			Builder.Append(CultureInfo.InvariantCulture, $"{SourcePrefix}{Wildcard}{SourceSuffix} {TargetPrefix}{Wildcard}{TargetSuffix}");
			return Builder.ToString();
		}
	}
}
