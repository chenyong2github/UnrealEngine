// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace MetadataTool
{
	class CompilePatternMatcher : PatternMatcher
	{
		static string[] SourceFileExtensions =
		{
			".cpp",
			".h",
			".cc",
			".hh",
			".m",
			".mm",
			".rc",
			".inl",
			".inc"
		};

		public override string Category => "Compile";

		public override bool TryMatch(InputJob Job, InputJobStep JobStep, InputDiagnostic Diagnostic, List<TrackedIssueFingerprint> Fingerprints)
		{
			// Find any files in compiler output format
			HashSet<string> SourceFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (Match FileMatch in Regex.Matches(Diagnostic.Message, @"^\s*(?:In file included from\s*)?((?:[A-Za-z]:)?[^\s(:]+)[\(:]\d", RegexOptions.Multiline))
			{
				if (FileMatch.Success)
				{
					string FileName = GetNormalizedFileName(FileMatch.Groups[1].Value, JobStep.BaseDirectory);
					if (SourceFileExtensions.Any(x => FileName.EndsWith(x, StringComparison.OrdinalIgnoreCase)))
					{
						SourceFileNames.Add(FileName);
					}
				}
			}

			// If we found any source files, create a diagnostic category for them
			if (SourceFileNames.Count > 0)
			{
				TrackedIssueFingerprint Fingerprint = new TrackedIssueFingerprint(Category, GetSummary(SourceFileNames), Diagnostic.Url, Job.Change);
				Fingerprint.Details.Add(Diagnostic.Message);
				Fingerprint.FileNames.UnionWith(SourceFileNames);
				Fingerprints.Add(Fingerprint);
				return true;
			}

			// Otherwise pass
			return false;
		}

		public override bool TryMerge(TrackedIssueFingerprint Source, TrackedIssueFingerprint Target)
		{
			if(base.TryMerge(Source, Target))
			{
				Target.Summary = GetSummary(Target.FileNames);
				return true;
			}
			return false;
		}

		string GetSummary(IEnumerable<string> SourceFileNames)
		{
			SortedSet<string> ShortFileNames = TrackedIssueFingerprint.GetSourceFileNames(SourceFileNames);
			if (ShortFileNames.Count == 0)
			{
				return "Compile errors";
			}
			else
			{
				return String.Format("Compile errors in {0}", String.Join(", ", ShortFileNames));
			}
		}
	}
}
