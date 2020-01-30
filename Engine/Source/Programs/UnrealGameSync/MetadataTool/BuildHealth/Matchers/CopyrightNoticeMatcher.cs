// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace MetadataTool
{
	class CopyrightNoticeMatcher : GenericCodePatternMatcher
	{
		public override string Category => "Copyright";

		public override bool TryMatch(InputJob Job, InputJobStep JobStep, InputDiagnostic Diagnostic, List<BuildHealthIssue> Issues)
		{
			// Make sure we're running a step that this applies to
			if(JobStep.Name.IndexOf("Copyright", StringComparison.OrdinalIgnoreCase) == -1)
			{
				return false;
			}

			// Find any files in compiler output format
			HashSet<string> SourceFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (Match FileMatch in Regex.Matches(Diagnostic.Message, @"^\s*(?:WARNING|ERROR):\s*([^ ]+\.[a-zA-Z]+):", RegexOptions.Multiline))
			{
				if (FileMatch.Success)
				{
					string SourceFileName = FileMatch.Groups[1].Value.Replace('\\', '/');
					SourceFileNames.Add(SourceFileName);
				}
			}

			// If we found any source files, create a diagnostic category for them
			if (SourceFileNames.Count > 0)
			{
				BuildHealthIssue Issue = new BuildHealthIssue(Job.Project, Category, Job.Url, new BuildHealthDiagnostic(JobStep.Name, JobStep.Url, Diagnostic.Message, Diagnostic.Url));
				Issue.FileNames.UnionWith(SourceFileNames);
				Issues.Add(Issue);
				return true;
			}

			// Otherwise pass
			return false;
		}

		public override string GetSummary(BuildHealthIssue Issue)
		{
			SortedSet<string> ShortFileNames = GetSourceFileNames(Issue.FileNames);
			if (ShortFileNames.Count == 0)
			{
				return "Missing copyright notices";
			}
			else
			{
				return String.Format("Missing copyright in {0}", String.Join(", ", ShortFileNames));
			}
		}
	}
}
