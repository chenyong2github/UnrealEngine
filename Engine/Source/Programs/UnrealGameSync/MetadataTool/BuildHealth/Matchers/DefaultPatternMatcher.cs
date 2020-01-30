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
	class DefaultPatternMatcher : PatternMatcher
	{
		public override string Category => "Default";

		public override bool TryMatch(InputJob Job, InputJobStep JobStep, InputDiagnostic Diagnostic, List<BuildHealthIssue> Issues)
		{
			string DefaultProject = String.Format("{0} (Unmatched)", Job.Project);

			BuildHealthIssue Issue = new BuildHealthIssue(DefaultProject, Category, Job.Url, new BuildHealthDiagnostic(JobStep.Name, JobStep.Url, Diagnostic.Message, Diagnostic.Url));
			Issue.Identifiers.Add(Diagnostic.Message);
			Issues.Add(Issue);
			
			return true;
		}

		public override List<ChangeInfo> FindCausers(PerforceConnection Perforce, BuildHealthIssue Issue, IReadOnlyList<ChangeInfo> Changes)
		{
			return new List<ChangeInfo>();
		}

		public override string GetSummary(BuildHealthIssue Issue)
		{
			string Message = Issue.Diagnostics[0].Message;

			int Idx = Message.IndexOf('\n');
			if(Idx >= 0)
			{
				Message = Message.Substring(0, Idx).Trim();
			}

			return Message;
		}
	}
}
