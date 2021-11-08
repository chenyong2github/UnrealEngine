// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
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
		public LogEventMatch? Match(ILogCursor Cursor)
		{
			int LineCount = 0;
			bool bIsError = false;
			bool bIsWarning = false;
			while (Cursor.IsMatch(LineCount, @": undefined reference to |undefined symbol|^\s*(ld|ld.lld):|^[^:]*[^a-zA-Z][a-z]?ld: |^\s*>>>"))
			{
				bIsError |= Cursor.IsMatch("error:");
				bIsWarning |= Cursor.IsMatch("warning:");
				LineCount++;
			}
			if (LineCount > 0)
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);

				bool bHasSymbol = AddSymbolMarkupForLine(Builder);
				for (int Idx = 1; Idx < LineCount; Idx++)
				{
					Builder.MoveNext();
					bHasSymbol |= AddSymbolMarkupForLine(Builder);
				}
				for (; ; )
				{
					if (Builder.Next.IsMatch("ld:"))
					{
						break;
					}
					else if (Builder.Next.IsMatch("error:"))
					{
						bIsError = true;
					}
					else if (Builder.Next.IsMatch("warning:"))
					{
						bIsWarning = true;
					}
					else
					{
						break;
					}

					bHasSymbol |= AddSymbolMarkupForLine(Builder);
					Builder.MoveNext();
				}

				LogLevel Level = (bIsError || !bIsWarning) ? LogLevel.Error : LogLevel.Warning;
				EventId EventId = bHasSymbol ? KnownLogEvents.Linker_UndefinedSymbol : KnownLogEvents.Linker;
				return Builder.ToMatch(LogEventPriority.Normal, Level, EventId);
			}

			Match? Match;
			if (Cursor.TryMatch(@"^(\s*)Undefined symbols for architecture", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				AddSymbolMarkupForLine(Builder);

				string Prefix = $"^(?<prefix>{Match.Groups[1].Value}\\s+)";
				if (Builder.Next.TryMatch(Prefix + @"""(?<symbol>[^""]+)""", out Match))
				{
					Prefix = $"^{Match.Groups["prefix"].Value}\\s+";

					Builder.MoveNext();
					Builder.AnnotateSymbol(Match.Groups["symbol"]);

					while(Builder.Next.TryMatch(Prefix + "(?<symbol>[^ ].*) in ", out Match))
					{
						Builder.MoveNext();
						Builder.AnnotateSymbol(Match.Groups["symbol"]);
					}
				}

				while (Builder.Next.IsMatch(@"^\s*(ld|clang):"))
				{
					Builder.MoveNext();
				}

				return Builder.ToMatch(LogEventPriority.Normal, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
			}
			if (Cursor.TryMatch(@"error (?<code>LNK\d+):", out Match))
			{
				LogEventBuilder Builder = new LogEventBuilder(Cursor);
				AddSymbolMarkupForLine(Builder);

				Group CodeGroup = Match.Groups["code"];
				Builder.Annotate(CodeGroup, LogEventMarkup.ErrorCode);

				if (CodeGroup.Value.Equals("LNK2001", StringComparison.Ordinal) || CodeGroup.Value.Equals("LNK2019", StringComparison.Ordinal))
				{
					return Builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
				}
				else if (CodeGroup.Value.Equals("LNK2005", StringComparison.Ordinal) || CodeGroup.Value.Equals("LNK4022", StringComparison.Ordinal))
				{
					return Builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Linker_DuplicateSymbol);
				}
				else
				{
					return Builder.ToMatch(LogEventPriority.High, LogLevel.Error, KnownLogEvents.Linker);
				}
			}

			return null;
		}

		static bool AddSymbolMarkupForLine(LogEventBuilder Builder)
		{
			bool bHasSymbols = false;

			string? Message = Builder.Current.CurrentLine;

			// Mac link error:
			//   Undefined symbols for architecture arm64:
			//     "Foo::Bar() const", referenced from:
			Match SymbolMatch = Regex.Match(Message, "^  \"(?<symbol>.+)\"");
			if (SymbolMatch.Success)
			{
				Builder.AnnotateSymbol(SymbolMatch.Groups[1]);
				bHasSymbols = true;
			}

			// Android link error:
			//   Foo.o:(.data.rel.ro + 0x5d88): undefined reference to `Foo::Bar()'
			Match UndefinedReference = Regex.Match(Message, ": undefined reference to [`'](?<symbol>[^`']+)");
			if (UndefinedReference.Success)
			{
				Builder.AnnotateSymbol(UndefinedReference.Groups[1]);
				bHasSymbols = true;
			}

			// LLD link error:
			//   ld.lld.exe: error: undefined symbol: Foo::Bar() const
			Match LldMatch = Regex.Match(Message, "error: undefined symbol:\\s*(?<symbol>.+)");
			if (LldMatch.Success)
			{
				Builder.AnnotateSymbol(LldMatch.Groups[1]);
				bHasSymbols = true;
			}

			// Link error:
			//   Link: error: L0039: reference to undefined symbol `Foo::Bar() const' in file
			Match LinkMatch = Regex.Match(Message, ": reference to undefined symbol [`'](?<symbol>[^`']+)");
			if (LinkMatch.Success)
			{
				Builder.AnnotateSymbol(LinkMatch.Groups[1]);
				bHasSymbols = true;
			}
			Match LinkMultipleMatch = Regex.Match(Message, @": (?<symbol>[^\s]+) already defined in");
			if (LinkMultipleMatch.Success)
			{
				Builder.AnnotateSymbol(LinkMultipleMatch.Groups[1]);
				bHasSymbols = true;
			}

			// Microsoft linker error:
			//   Foo.cpp.obj : error LNK2001: unresolved external symbol \"private: virtual void __cdecl UAssetManager::InitializeAssetBundlesFromMetadata_Recursive(class UStruct const *,void const *,struct FAssetBundleData &,class FName,class TSet<void const *,struct DefaultKeyFuncs<void const *,0>,class FDefaultSetAllocator> &)const \" (?InitializeAssetBundlesFromMetadata_Recursive@UAssetManager@@EEBAXPEBVUStruct@@PEBXAEAUFAssetBundleData@@VFName@@AEAV?$TSet@PEBXU?$DefaultKeyFuncs@PEBX$0A@@@VFDefaultSetAllocator@@@@@Z)",
			Match MicrosoftMatch = Regex.Match(Message, " symbol \"(?<symbol>[^\"]*)\"");
			if (MicrosoftMatch.Success)
			{
				Builder.AnnotateSymbol(MicrosoftMatch.Groups[1]);
				bHasSymbols = true;
			}

			return bHasSymbols;
		}
	}
}
