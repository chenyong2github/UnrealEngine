// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace MetadataTool
{
	class CompilePatternMatcher : GenericCodePatternMatcher
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

		public override bool TryMatch(InputJob Job, InputJobStep JobStep, InputDiagnostic Diagnostic, List<BuildHealthIssue> Issues)
		{
			// Find a list of source files with errors
			HashSet<string> ErrorFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (Match FileMatch in Regex.Matches(Diagnostic.Message, @"^\s*((?:[A-Za-z]:)?[^\s(:]+)[\(:]\d[\s\d:\)]+(?:warning|error|fatal error)", RegexOptions.Multiline))
			{
				if (FileMatch.Success)
				{
					string FileName = GetNormalizedFileName(FileMatch.Groups[1].Value, JobStep.BaseDirectory);
					if (SourceFileExtensions.Any(x => FileName.EndsWith(x, StringComparison.OrdinalIgnoreCase)))
					{
						ErrorFileNames.Add(FileName);
					}
				}
			}

			// Find any referenced files in compiler output format
			HashSet<string> ReferencedFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			foreach (Match FileMatch in Regex.Matches(Diagnostic.Message, @"^\s*(?:In file included from\s*)?((?:[A-Za-z]:)?[^\s(:]+)[\(:]\d", RegexOptions.Multiline))
			{
				if (FileMatch.Success)
				{
					string FileName = GetNormalizedFileName(FileMatch.Groups[1].Value, JobStep.BaseDirectory);
					if (SourceFileExtensions.Any(x => FileName.EndsWith(x, StringComparison.OrdinalIgnoreCase)))
					{
						ReferencedFileNames.Add(FileName);
					}
				}
			}

			// If we found any source files, create a diagnostic category for them
			if (ReferencedFileNames.Count > 0)
			{
				BuildHealthIssue Issue = new BuildHealthIssue(Job.Project, Category, Job.Url, new BuildHealthDiagnostic(JobStep.Name, JobStep.Url, ShortenPaths(Diagnostic.Message), Diagnostic.Url));
				Issue.FileNames.UnionWith(ReferencedFileNames);
				Issue.Identifiers.UnionWith(GetSourceFileNames(ErrorFileNames));
				Issues.Add(Issue);
				return true;
			}

			// Otherwise pass
			return false;
		}

		static string ShortenPaths(string Text)
		{
			Text = Regex.Replace(Text, @"^(\s*(?:In file included from\s*)?)(?:[A-Za-z]:)?[^\s(:]+[/\\]([^\s/\\]+[\(:]\d)", "$1$2", RegexOptions.Multiline);
			return Text;
		}

		public override string GetSummary(BuildHealthIssue Issue)
		{
			if (Issue.Identifiers.Count == 0)
			{
				return "Compile errors";
			}
			else
			{
				return String.Format("Compile errors in {0}", String.Join(", ", Issue.Identifiers));
			}
		}
	}
}
