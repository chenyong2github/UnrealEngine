// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public class UhtScriptStructParser : UhtScriptStruct
	{
		private static UhtKeywordTable KeywordTable = UhtKeywordTables.Instance.Get(UhtTableNames.ScriptStruct);
		private static UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.ScriptStruct);

		public UhtToken SuperIdentifier;
		public List<UhtToken[]>? BaseIdentifiers = null;

		public UhtScriptStructParser(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		protected override void ResolveSuper(UhtResolvePhase ResolvePhase)
		{
			base.ResolveSuper(ResolvePhase);

			switch (ResolvePhase)
			{
				case UhtResolvePhase.Bases:
					BindAndResolveSuper(ref this.SuperIdentifier, UhtFindOptions.ScriptStruct);

					// if we have a base struct, propagate inherited struct flags now
					UhtScriptStruct? SuperScriptStruct = this.SuperScriptStruct;
					if (SuperScriptStruct != null)
					{
						this.ScriptStructFlags |= SuperScriptStruct.ScriptStructFlags & EStructFlags.Inherit;
					}
					break;
			}
		}

		protected override bool ResolveSelf(UhtResolvePhase ResolvePhase)
		{
			bool bResult = base.ResolveSelf(ResolvePhase);

			switch (ResolvePhase)
			{
				case UhtResolvePhase.Properties:
					UhtPropertyParser.ResolveChildren(this, UhtPropertyParseOptions.AddModuleRelativePath);
					break;
			}
			return bResult;
		}

		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Global)]
		private static UhtParseResult USTRUCTKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return ParseUScriptStruct(TopScope, Token);
		}

		[UhtKeyword(Extends = UhtTableNames.ScriptStruct)]
		[UhtKeyword(Extends = UhtTableNames.ScriptStruct, Keyword = "GENERATED_BODY")]
		private static UhtParseResult GENERATED_USTRUCT_BODYKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)TopScope.ScopeType;

			if (TopScope.AccessSpecifier != UhtAccessSpecifier.Public)
			{
				TopScope.TokenReader.LogError($"{Token.Value} must be in the public scope of '{ScriptStruct.SourceName}', not private or protected.");
			}

			if (ScriptStruct.MacroDeclaredLineNumber != -1)
			{
				TopScope.TokenReader.LogError($"Multiple {Token.Value} declarations found in '{ScriptStruct.SourceName}'");
			}

			ScriptStruct.MacroDeclaredLineNumber = TopScope.TokenReader.InputLine;

			UhtParserHelpers.ParseCompileVersionDeclaration(TopScope.TokenReader, ScriptStruct);

			TopScope.TokenReader.Optional(';');
			return UhtParseResult.Handled;
		}

		[UhtKeyword(Extends = UhtTableNames.ScriptStruct)]
		private static UhtParseResult RIGVM_METHODKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			ParseRigVM(TopScope);
			return UhtParseResult.Handled;
		}
		#endregion

		private static UhtParseResult ParseUScriptStruct(UhtParsingScope ParentScope, UhtToken KeywordToken)
		{
			UhtScriptStructParser ScriptStruct = new UhtScriptStructParser(ParentScope.ScopeType, KeywordToken.InputLine);
			using (var TopScope = new UhtParsingScope(ParentScope, ScriptStruct, KeywordTable, UhtAccessSpecifier.Public))
			{
				const string ScopeName = "struct";

				using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, ScopeName))
				{
					// Parse the specifiers
					UhtSpecifierContext SpecifierContext = new UhtSpecifierContext(TopScope, TopScope.TokenReader, ScriptStruct.MetaData);
					UhtSpecifierParser Specifiers = TopScope.HeaderParser.GetSpecifierParser(SpecifierContext, ScopeName, SpecifierTable);
					Specifiers.ParseSpecifiers();

					// Consume the struct specifier
					TopScope.TokenReader.Require("struct");

					TopScope.TokenReader.SkipAlignasAndDeprecatedMacroIfNecessary();

					// Read the struct name and possible API macro name
					UhtToken APIMacroToken;
					TopScope.TokenReader.TryOptionalAPIMacro(out APIMacroToken);
					ScriptStruct.SourceName = TopScope.TokenReader.GetIdentifier().Value.ToString();

					TopScope.AddModuleRelativePathToMetaData();

					// Strip the name
					if (ScriptStruct.SourceName[0] == 'T' || ScriptStruct.SourceName[0] == 'F')
					{
						ScriptStruct.EngineName = ScriptStruct.SourceName.Substring(1);
					}
					else
					{
						// This will be flagged later in the validation phase
						ScriptStruct.EngineName = ScriptStruct.SourceName;
					}

					// Check for an empty engine name
					if (ScriptStruct.EngineName.Length == 0)
					{
						TopScope.TokenReader.LogError($"When compiling struct definition for '{ScriptStruct.SourceName}', attempting to strip prefix results in an empty name. Did you leave off a prefix?");
					}

					// Parse the inheritance
					UhtParserHelpers.ParseInheritance(TopScope.TokenReader, out ScriptStruct.SuperIdentifier, out ScriptStruct.BaseIdentifiers);

					// Add the comments here for compatibility with old UHT
					//COMPATIBILITY-TODO - Move this back to where the AddModuleRelativePathToMetaData is called.
					TopScope.TokenReader.PeekToken();
					TopScope.TokenReader.CommitPendingComments();
					TopScope.AddFormattedCommentsAsTooltipMetaData();

					// Initialize the structure flags
					ScriptStruct.ScriptStructFlags |= EStructFlags.Native;

					// Record that this struct is RequiredAPI if the CORE_API style macro was present
					if (APIMacroToken)
					{
						ScriptStruct.ScriptStructFlags |= EStructFlags.RequiredAPI;
					}

					// Process the deferred specifiers
					Specifiers.ParseDeferred();

					if (ScriptStruct.Outer != null)
					{
						ScriptStruct.Outer.AddChild(ScriptStruct);
					}

					TopScope.HeaderParser.ParseStatements('{', '}', true);

					TopScope.TokenReader.Require(';');

					if (ScriptStruct.MacroDeclaredLineNumber == -1 && ScriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native))
					{
						TopScope.TokenReader.LogError("Expected a GENERATED_BODY() at the start of the struct");
					}
				}
				return UhtParseResult.Handled;
			}
		}

		private static void ParseRigVM(UhtParsingScope TopScope)
		{
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)TopScope.ScopeType;

			using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, "RIGVM_METHOD"))
			{

				// Create the RigVM information if it doesn't already exist
				UhtRigVMStructInfo? StructInfo = ScriptStruct.RigVMStructInfo;
				if (StructInfo == null)
				{
					StructInfo = new UhtRigVMStructInfo();
					StructInfo.Name = ScriptStruct.EngineName;
					ScriptStruct.RigVMStructInfo = StructInfo;
				}

				// Create a new method information and add it
				UhtRigVMMethodInfo MethodInfo = new UhtRigVMMethodInfo();

				// NOTE: The argument list reader doesn't handle templates with multiple arguments (i.e. the ',' issue)
				TopScope.TokenReader
					.RequireList('(', ')')
					.Optional("virtual")
					.RequireIdentifier((ref UhtToken Identifier) => MethodInfo.ReturnType = Identifier.Value.ToString())
					.RequireIdentifier((ref UhtToken Identifier) => MethodInfo.Name = Identifier.Value.ToString());

				bool bIsGetUpgradeInfo = false;
				if (MethodInfo.ReturnType == "FRigVMStructUpgradeInfo" && MethodInfo.Name == "GetUpgradeInfo")
				{
					StructInfo.bHasGetUpgradeInfoMethod = true;
					bIsGetUpgradeInfo = true;
				}

				TopScope.TokenReader
					.RequireList('(', ')', ',', false, (IEnumerable<UhtToken> Tokens) =>
					{
						if (!bIsGetUpgradeInfo)
						{
							StringViewBuilder Builder = new StringViewBuilder();
							UhtToken LastToken = new UhtToken();
							foreach (UhtToken Token in Tokens)
							{
								if (Token.IsSymbol('='))
								{
									break;
								}
								if (LastToken)
								{
									if (Builder.Length != 0)
									{
										Builder.Append(' ');
									}
									Builder.Append(LastToken.Value);
								}
								LastToken = Token;
							}
							MethodInfo.Parameters.Add(new UhtRigVMParameter(LastToken.Value.ToString(), Builder.ToString()));
						}
					})
					.ConsumeUntil(';');

				if (!bIsGetUpgradeInfo)
				{
					StructInfo.Methods.Add(MethodInfo);
				}
			}
		}
	}
}
