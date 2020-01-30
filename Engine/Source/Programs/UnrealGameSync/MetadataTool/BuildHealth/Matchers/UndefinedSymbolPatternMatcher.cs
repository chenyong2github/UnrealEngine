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
	class UndefinedSymbolPatternMatcher : GenericCodePatternMatcher
	{
		public override string Category => "UndefinedSymbol";

		public override bool TryMatch(InputJob Job, InputJobStep JobStep, InputDiagnostic Diagnostic, List<BuildHealthIssue> Issues)
		{
			List<string> SymbolMatches = new List<string>();

			// Mac link error:
			//   Undefined symbols for architecture arm64:
            //     "Foo::Bar() const", referenced from:
			if (Regex.IsMatch(Diagnostic.Message, "^Undefined symbols"))
			{
				foreach(string Line in Diagnostic.Message.Split('\n'))
				{
					Match SymbolMatch = Regex.Match(Line, "^  \"(.+)\"");
					if (SymbolMatch.Success)
					{
						SymbolMatches.Add(SymbolMatch.Groups[1].Value);
					}
				}
			}

			// Android link error:
			//   Foo.o:(.data.rel.ro + 0x5d88): undefined reference to `Foo::Bar()'
			Match UndefinedReference = Regex.Match(Diagnostic.Message, ": undefined reference to [`']([^`']+)");
			if(UndefinedReference.Success)
			{
				SymbolMatches.Add(UndefinedReference.Groups[1].Value);
			}

			// LLD link error:
			//   ld.lld.exe: error: undefined symbol: Foo::Bar() const
			Match LldMatch = Regex.Match(Diagnostic.Message, "error: undefined symbol:\\s*(.+)");
			if (LldMatch.Success)
			{
				SymbolMatches.Add(LldMatch.Groups[1].Value);
			}

			// Link error:
			//   Link: error: L0039: reference to undefined symbol `Foo::Bar() const' in file
			Match LinkMatch = Regex.Match(Diagnostic.Message, ": reference to undefined symbol [`']([^`']+)");
			if (LinkMatch.Success)
			{
				SymbolMatches.Add(LinkMatch.Groups[1].Value);
			}

			// Microsoft linker error:
			//   Foo.cpp.obj : error LNK2001: unresolved external symbol \"private: virtual void __cdecl UAssetManager::InitializeAssetBundlesFromMetadata_Recursive(class UStruct const *,void const *,struct FAssetBundleData &,class FName,class TSet<void const *,struct DefaultKeyFuncs<void const *,0>,class FDefaultSetAllocator> &)const \" (?InitializeAssetBundlesFromMetadata_Recursive@UAssetManager@@EEBAXPEBVUStruct@@PEBXAEAUFAssetBundleData@@VFName@@AEAV?$TSet@PEBXU?$DefaultKeyFuncs@PEBX$0A@@@VFDefaultSetAllocator@@@@@Z)",
			Match MicrosoftMatch = Regex.Match(Diagnostic.Message, ": unresolved external symbol \"([^\"]*)\"");
			if(MicrosoftMatch.Success)
			{
				SymbolMatches.Add(MicrosoftMatch.Groups[1].Value);
			}

			// Clean up all the symbol names
			SortedSet<string> SymbolNames = new SortedSet<string>(StringComparer.Ordinal);
			foreach(string SymbolMatch in SymbolMatches)
			{
				string SymbolName = SymbolMatch;

				// Remove any __declspec qualifiers
				SymbolName = Regex.Replace(SymbolName, "(?<![^a-zA-Z_])__declspec\\([^\\)]+\\)", "");

				// Remove any argument lists for functions (anything after the first paren)
				SymbolName = Regex.Replace(SymbolName, "\\(.*$", "");

				// Remove any decorators and type information (greedy match up to the last space)
				SymbolName = Regex.Replace(SymbolName, "^.* ", "");

				// Add it to the list
				SymbolNames.Add(SymbolName);
			}

			// If we found any symbol names, create a fingerprint for them
			if (SymbolNames.Count > 0)
			{
				BuildHealthIssue Issue = new BuildHealthIssue(Job.Project, Category, Job.Url, new BuildHealthDiagnostic(JobStep.Name, JobStep.Url, Diagnostic.Message, Diagnostic.Url));
				Issue.Identifiers.UnionWith(SymbolNames);
				Issues.Add(Issue);
				return true;
			}

			// Otherwise pass
			return false;
		}

		public override List<ChangeInfo> FindCausers(PerforceConnection Perforce, BuildHealthIssue Issue, IReadOnlyList<ChangeInfo> Changes)
		{
			SortedSet<string> FileNamesWithoutPath = new SortedSet<string>(StringComparer.Ordinal);
			foreach(string Identifier in Issue.Identifiers)
			{
				Match Match = Regex.Match(Identifier, "^[AUFS]([A-Z][A-Za-z_]*)::");
				if(Match.Success)
				{
					FileNamesWithoutPath.Add(Match.Groups[1].Value + ".h");
					FileNamesWithoutPath.Add(Match.Groups[1].Value + ".cpp");
				}
			}

			if(FileNamesWithoutPath.Count > 0)
			{
				List<ChangeInfo> Causers = new List<ChangeInfo>();
				foreach (ChangeInfo Change in Changes)
				{
					DescribeRecord DescribeRecord = GetDescribeRecord(Perforce, Change);
					if (ContainsFileNames(DescribeRecord, FileNamesWithoutPath))
					{
						Causers.Add(Change);
					}
				}

				if (Causers.Count > 0)
				{
					return Causers;
				}
			}

			return base.FindCausers(Perforce, Issue, Changes);
		}

		public override string GetSummary(BuildHealthIssue Issue)
		{
			if(Issue.Identifiers.Count == 1)
			{
				return String.Format("Undefined symbol '{0}'", Issue.Identifiers.First());
			}
			else
			{
				return String.Format("Undefined symbols - '{0}'", String.Join("', '", Issue.Identifiers));
			}
		}
	}
}
