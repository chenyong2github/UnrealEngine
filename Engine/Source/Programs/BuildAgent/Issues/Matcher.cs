// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Tools.DotNETCommon.Perforce;

namespace BuildAgent.Issues
{
	/// <summary>
	/// Class that implements a pattern matcher for a particular class of errors
	/// </summary>
	abstract class Matcher
	{
		/// <summary>
		/// The category name
		/// </summary>
		public abstract string Category
		{
			get;
		}

		/// <summary>
		/// Creates fingerprints from any matching diagnostics
		/// </summary>
		/// <param name="Job">The job that was run</param>
		/// <param name="JobStep">The job step that was run</param>
		/// <param name="Diagnostics">List of diagnostics that were produced by the build. Items should be removed from this list if they match.</param>
		/// <param name="Issues">List which receives all the matched issues.</param>
		public virtual void Match(InputJob Job, InputJobStep JobStep, List<InputDiagnostic> Diagnostics, List<Issue> Issues)
		{
			for (int Idx = 0; Idx < Diagnostics.Count; Idx++)
			{
				InputDiagnostic Diagnostic = Diagnostics[Idx];
				if(TryMatch(Job, JobStep, Diagnostic, Issues))
				{
					Diagnostics.RemoveAt(Idx);
					Idx--;
				}
			}
		}

		/// <summary>
		/// Tries to create a fingerprint from an individual diagnostic.
		/// </summary>
		/// <param name="Job">The job that was run</param>
		/// <param name="JobStep">The job step that was run</param>
		/// <param name="Diagnostic">A diagnostic from the given job step</param>
		/// <param name="Issues">List which receives all the matched issues.</param>
		/// <returns>True if this diagnostic should be removed (usually because a fingerprint was created)</returns>
		public abstract bool TryMatch(InputJob Job, InputJobStep JobStep, InputDiagnostic Diagnostic, List<Issue> Issues);

		/// <summary>
		/// Determines if one issue can be merged into another
		/// </summary>
		/// <param name="Source">The source issue</param>
		/// <param name="Target">The target issue</param>
		public virtual bool CanMerge(Issue Source, Issue Target)
		{
			// Make sure the categories match
			if (Source.Category != Target.Category)
			{
				return false;
			}

			// Check that a filename or message matches
			if (!Source.FileNames.Any(x => Target.FileNames.Contains(x)) && !Source.Identifiers.Any(x => Target.Identifiers.Contains(x)))
			{
				return false;
			}

			return true;
		}

		/// <summary>
		/// Determines if an issue can be merged into another issue that occurred at the same initial job
		/// </summary>
		/// <param name="Source">The source issue</param>
		/// <param name="Target">The target issue</param>
		/// <returns>True if the two new issues can be merged</returns>
		public virtual bool CanMergeInitialJob(Issue Source, Issue Target)
		{
			return Source.Category == Target.Category;
		}

		/// <summary>
		/// Merge one fingerprint with another
		/// </summary>
		/// <param name="Source">The source fingerprint</param>
		/// <param name="Target">The fingerprint to merge into</param>
		public virtual void Merge(Issue Source, Issue Target)
		{
			HashSet<string> TargetMessages = new HashSet<string>(Target.Diagnostics.Select(x => x.Message), StringComparer.Ordinal);
			foreach(IssueDiagnostic SourceDiagnostic in Source.Diagnostics)
			{
				if(Target.Diagnostics.Count >= 50)
				{
					break;
				}
				if(!TargetMessages.Contains(SourceDiagnostic.Message))
				{
					Target.Diagnostics.Add(SourceDiagnostic);
				}
			}

			Target.FileNames.UnionWith(Source.FileNames);
			Target.Identifiers.UnionWith(Source.Identifiers);
			Target.References.UnionWith(Source.References);
		}

		/// <summary>
		/// Filters all the likely causers from the list of changes since an issue was created
		/// </summary>
		/// <param name="Perforce">The perforce connection</param>
		/// <param name="Issue">The build issue</param>
		/// <param name="Changes">List of changes since the issue first occurred.</param>
		/// <returns>List of changes which are causers for the issue</returns>
		public virtual List<ChangeInfo> FindCausers(PerforceConnection Perforce, Issue Issue, IReadOnlyList<ChangeInfo> Changes)
		{
			List<ChangeInfo> Causers = new List<ChangeInfo>();

			SortedSet<string> FileNamesWithoutPath = GetFileNamesWithoutPath(Issue.FileNames);
			if (FileNamesWithoutPath.Count > 0)
			{
				foreach (ChangeInfo Change in Changes)
				{
					DescribeRecord DescribeRecord = GetDescribeRecord(Perforce, Change);
					if (ContainsFileNames(DescribeRecord, FileNamesWithoutPath))
					{
						Causers.Add(Change);
					}
				}
			}

			if(Causers.Count > 0)
			{
				return Causers;
			}
			else
			{
				return new List<ChangeInfo>(Changes);
			}
		}

		/// <summary>
		/// Utility method to get the describe record for a change. Caches it on the ChangeInfo object as necessary.
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Change">The change to query</param>
		public DescribeRecord GetDescribeRecord(PerforceConnection Perforce, ChangeInfo Change)
		{
			if(Change.CachedDescribeRecord == null)
			{
				Change.CachedDescribeRecord = Perforce.Describe(Change.Record.Number).Data;
			}
			return Change.CachedDescribeRecord;
		}

		/// <summary>
		/// Tests whether a change is a code change
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Change">The change to query</param>
		/// <returns>True if the change is a code change</returns>
		public bool ContainsAnyFileWithExtension(PerforceConnection Perforce, ChangeInfo Change, string[] Extensions)
		{
			DescribeRecord Record = GetDescribeRecord(Perforce, Change);
			foreach(DescribeFileRecord File in Record.Files)
			{
				foreach(string Extension in Extensions)
				{
					if(File.DepotFile.EndsWith(Extension, StringComparison.OrdinalIgnoreCase))
					{
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Determines if this change is a likely causer for an issue
		/// </summary>
		/// <param name="DescribeRecord">The change describe record</param>
		/// <param name="Fingerprint">Fingerprint for the issue</param>
		/// <returns>True if the change is a likely culprit</returns>
		protected static bool ContainsFileNames(DescribeRecord DescribeRecord, SortedSet<string> FileNamesWithoutPath)
		{
			foreach (DescribeFileRecord File in DescribeRecord.Files)
			{
				int Idx = File.DepotFile.LastIndexOf('/');
				if (Idx != -1)
				{
					string FileName = File.DepotFile.Substring(Idx + 1);
					if (FileNamesWithoutPath.Contains(FileName))
					{
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Normalizes a filename to a path within the workspace
		/// </summary>
		/// <param name="FileName">Filename to normalize</param>
		/// <param name="BaseDirectory">Base directory containing the workspace</param>
		/// <returns>Normalized filename</returns>
		protected string GetNormalizedFileName(string FileName, string BaseDirectory)
		{
			string NormalizedFileName = FileName.Replace('\\', '/');

			const string StandardEnginePrefix = "../../../";
			if (NormalizedFileName.StartsWith(StandardEnginePrefix))
			{
				NormalizedFileName = NormalizedFileName.Substring(StandardEnginePrefix.Length);
			}
			else if (!String.IsNullOrEmpty(BaseDirectory))
			{
				// Normalize the expected base directory for errors in this build, and attempt to strip it from the file name
				string NormalizedBaseDirectory = BaseDirectory;
				if (NormalizedBaseDirectory != null && NormalizedBaseDirectory.Length > 0)
				{
					NormalizedBaseDirectory = NormalizedBaseDirectory.Replace('\\', '/').TrimEnd('/') + "/";
				}
				if (NormalizedFileName.StartsWith(NormalizedBaseDirectory, StringComparison.OrdinalIgnoreCase))
				{
					NormalizedFileName = NormalizedFileName.Substring(NormalizedBaseDirectory.Length);
				}
			}
			else
			{
				// Try to match anything under a 'Sync' folder.
				Match FallbackRegex = Regex.Match(NormalizedFileName, "/Sync/(.*)");
				if (FallbackRegex.Success)
				{
					NormalizedFileName = FallbackRegex.Groups[1].Value;
				}
			}
			return NormalizedFileName;
		}

		/// <summary>
		/// Finds all the unique filenames without their path components
		/// </summary>
		/// <returns>Set of sorted filenames</returns>
		protected static SortedSet<string> GetFileNamesWithoutPath(IEnumerable<string> FileNames)
		{
			SortedSet<string> FileNamesWithoutPath = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in FileNames)
			{
				int Idx = FileName.LastIndexOf('/');
				if (Idx != -1)
				{
					FileNamesWithoutPath.Add(FileName.Substring(Idx + 1));
				}
			}
			return FileNamesWithoutPath;
		}

		/// <summary>
		/// Gets a set of unique source file names that relate to this issue
		/// </summary>
		/// <returns>Set of source file names</returns>
		protected static SortedSet<string> GetSourceFileNames(IEnumerable<string> FileNames)
		{
			SortedSet<string> ShortFileNames = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in FileNames)
			{
				int Idx = FileName.LastIndexOfAny(new char[] { '/', '\\' });
				if (Idx != -1)
				{
					string ShortFileName = FileName.Substring(Idx + 1);
					if (!ShortFileName.StartsWith("Module.", StringComparison.OrdinalIgnoreCase))
					{
						ShortFileNames.Add(ShortFileName);
					}
				}
			}
			return ShortFileNames;
		}

		/// <summary>
		/// Gets a set of unique asset filenames that relate to this issue
		/// </summary>
		/// <returns>Set of asset names</returns>
		protected static SortedSet<string> GetAssetNames(IEnumerable<string> FileNames)
		{
			SortedSet<string> ShortFileNames = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (string FileName in FileNames)
			{
				int Idx = FileName.LastIndexOfAny(new char[] { '/', '\\' });
				if (Idx != -1)
				{
					string AssetName = FileName.Substring(Idx + 1);

					int DotIdx = AssetName.LastIndexOf('.');
					if (DotIdx != -1)
					{
						AssetName = AssetName.Substring(0, DotIdx);
					}

					ShortFileNames.Add(AssetName);
				}
			}
			return ShortFileNames;
		}

		/// <summary>
		/// Gets the summary for an issue
		/// </summary>
		/// <param name="Issue">The issue to summarize</param>
		/// <returns>The summary text for this issue</returns>
		public abstract string GetSummary(Issue Issue);
	}
}
