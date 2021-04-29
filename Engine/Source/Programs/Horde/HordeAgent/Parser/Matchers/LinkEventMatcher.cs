// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser;
using HordeAgent.Parser.Interfaces;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeAgent.Parser.Matchers
{
	/// <summary>
	/// Matcher for linker errors
	/// </summary>
	class LinkEventMatcher : ILogEventMatcher
	{
		/// <inheritdoc/>
		public LogEvent? Match(ILogCursor Cursor, ILogContext Context)
		{
			Match? Match;

			int LineCount = 0;
			bool bIsWarning = true;
			while (Cursor.IsMatch(LineCount, @": In function |: undefined reference to |undefined symbol|^\s*(ld|ld.lld):|[^a-zA-Z]ld: |^\s*>>>"))
			{
				bIsWarning &= Cursor.IsMatch("warning:");
				LineCount++;
			}
			if (Cursor.IsMatch(LineCount, @"error: linker command failed with exit code "))
			{
				bIsWarning = false;
				LineCount++;
			}
			if (LineCount > 0)
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				Builder.LineCount = LineCount;
				EventId EventId = AddSymbolMarkup(Builder) ? KnownLogEvents.Linker_UndefinedSymbol : KnownLogEvents.Linker;
				return Builder.ToLogEvent(LogEventPriority.Normal, bIsWarning? LogLevel.Warning : LogLevel.Error, EventId);
			}

			if (Cursor.TryMatch(@"^(\s*)Undefined symbols for architecture", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);

				string Prefix = $"^(?<prefix>{Match.Groups[1].Value}\\s+)";
				if (Cursor.TryMatch(Builder.MaxOffset + 1, Prefix + @"""(?<symbol>[^""]+)""", out Match))
				{
					Prefix = $"^{Match.Groups["prefix"].Value}\\s+";

					Builder.MaxOffset++;
					Builder.Lines[Builder.MaxOffset].AddSpan(Match.Groups["symbol"]).MarkAsSymbol();

					while(Cursor.TryMatch(Builder.MaxOffset + 1, Prefix + "(?<symbol>[^ ].*) in ", out Match))
					{
						Builder.MaxOffset++;
//						Builder.Lines[Builder.MaxOffset].AddSpan(Match.Groups["symbol"]).MarkAsSymbol();
					}
				}

				Builder.MaxOffset = Cursor.MatchForwards(Builder.MaxOffset, @"^\s*(ld|clang):");

				AddSymbolMarkup(Builder);
				return Builder.ToLogEvent(LogEventPriority.Normal, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
			}
			if (Cursor.TryMatch(@"error (?<code>LNK\d+):", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);

				LogEventLine FirstLine = Builder.Lines[0];

				Group CodeGroup = Match.Groups["code"];
				FirstLine.AddSpan(CodeGroup).MarkAsErrorCode();
				AddSymbolMarkup(Builder);

				if (CodeGroup.Value.Equals("LNK2001", StringComparison.Ordinal) || CodeGroup.Value.Equals("LNK2019", StringComparison.Ordinal))
				{
					return Builder.ToLogEvent(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
				}
				else if (CodeGroup.Value.Equals("LNK2005", StringComparison.Ordinal) || CodeGroup.Value.Equals("LNK4022", StringComparison.Ordinal))
				{
					return Builder.ToLogEvent(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Linker_DuplicateSymbol);
				}
				else
				{
					return Builder.ToLogEvent(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Linker);
				}
			}

			return null;
		}

		static bool AddSymbolMarkup(LogEventBuilder Builder)
		{
			bool bHasSymbols = false;

			const string SpanName = "symbol";
			foreach (LogEventLine Line in Builder.Lines)
			{
				if(Line.Spans.ContainsKey(SpanName))
				{
					continue;
				}

				string Message = Line.Text;

				// Mac link error:
				//   Undefined symbols for architecture arm64:
				//     "Foo::Bar() const", referenced from:
				Match SymbolMatch = Regex.Match(Message, "^  \"(.+)\"");
				if (SymbolMatch.Success)
				{
					Line.AddSpan(SymbolMatch.Groups[1], SpanName).MarkAsSymbol();
					bHasSymbols = true;
				}

				// Android link error:
				//   Foo.o:(.data.rel.ro + 0x5d88): undefined reference to `Foo::Bar()'
				Match UndefinedReference = Regex.Match(Message, ": undefined reference to [`']([^`']+)");
				if (UndefinedReference.Success)
				{
					Line.AddSpan(UndefinedReference.Groups[1], SpanName).MarkAsSymbol();
					bHasSymbols = true;
				}

				// LLD link error:
				//   ld.lld.exe: error: undefined symbol: Foo::Bar() const
				Match LldMatch = Regex.Match(Message, "error: undefined symbol:\\s*(.+)");
				if (LldMatch.Success)
				{
					Line.AddSpan(LldMatch.Groups[1], SpanName).MarkAsSymbol();
					bHasSymbols = true;
				}

				// Link error:
				//   Link: error: L0039: reference to undefined symbol `Foo::Bar() const' in file
				Match LinkMatch = Regex.Match(Message, ": reference to undefined symbol [`']([^`']+)");
				if (LinkMatch.Success)
				{
					Line.AddSpan(LinkMatch.Groups[1], SpanName).MarkAsSymbol();
					bHasSymbols = true;
				}
				Match LinkMultipleMatch = Regex.Match(Message, @": ([^\s]+) already defined in");
				if (LinkMultipleMatch.Success)
				{
					Line.AddSpan(LinkMultipleMatch.Groups[1], SpanName).MarkAsSymbol();
					bHasSymbols = true;
				}

				// Microsoft linker error:
				//   Foo.cpp.obj : error LNK2001: unresolved external symbol \"private: virtual void __cdecl UAssetManager::InitializeAssetBundlesFromMetadata_Recursive(class UStruct const *,void const *,struct FAssetBundleData &,class FName,class TSet<void const *,struct DefaultKeyFuncs<void const *,0>,class FDefaultSetAllocator> &)const \" (?InitializeAssetBundlesFromMetadata_Recursive@UAssetManager@@EEBAXPEBVUStruct@@PEBXAEAUFAssetBundleData@@VFName@@AEAV?$TSet@PEBXU?$DefaultKeyFuncs@PEBX$0A@@@VFDefaultSetAllocator@@@@@Z)",
				Match MicrosoftMatch = Regex.Match(Message, " symbol \"([^\"]*)\"");
				if (MicrosoftMatch.Success)
				{
					Line.AddSpan(MicrosoftMatch.Groups[1], SpanName).MarkAsSymbol();
					bHasSymbols = true;
				}
			}

			return bHasSymbols;
		}
	}
}
