// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Tools.DotNETCommon.Perforce;

namespace MetadataTool
{
	class ContentPatternMatcher : GenericContentPatternMatcher
	{
		public override string Category => "Content";

		public override bool TryMatch(InputJob Job, InputJobStep JobStep, InputDiagnostic Diagnostic, List<BuildHealthIssue> Issues)
		{
			HashSet<string> FileNames = new HashSet<string>();
			foreach(Match Match in Regex.Matches(Diagnostic.Message, @"^\s*Log[a-zA-Z0-9]+:\s+(?:Error:|Warning:)\s+((?:[a-zA-Z]:)?[^:]+(?:.uasset|.umap)):\s*(.*)"))
			{
				FileNames.Add(GetNormalizedFileName(Match.Groups[1].Value, JobStep.BaseDirectory));
			}

			if(FileNames.Count > 0)
			{
				BuildHealthIssue Issue = new BuildHealthIssue(Job.Project, Category, Job.Url, new BuildHealthDiagnostic(JobStep.Name, JobStep.Url, Diagnostic.Message, Diagnostic.Url));
				Issue.FileNames.UnionWith(FileNames);
				Issues.Add(Issue);
				return true;
			}
			return false;
		}

		public override string GetSummary(BuildHealthIssue Issue)
		{
			if (Issue.FileNames.Count == 0)
			{
				return "Content errors";
			}
			else
			{
				return String.Format("Content errors in {0}", String.Join(", ", GetFileNamesWithoutPath(Issue.FileNames)));
			}
		}
	}
}
