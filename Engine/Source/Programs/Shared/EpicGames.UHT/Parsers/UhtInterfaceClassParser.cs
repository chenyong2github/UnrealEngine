// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public class UhtInterfaceClassParser : UhtClassBaseParser
	{
		private static UhtKeywordTable KeywordTable = UhtKeywordTables.Instance.Get(UhtTableNames.Interface);
		private static UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.Interface);

		public UhtInterfaceClassParser(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		protected override void ResolveSuper(UhtResolvePhase ResolvePhase)
		{
			base.ResolveSuper( ResolvePhase);
			switch (ResolvePhase)
			{
				case UhtResolvePhase.Bases:
					UhtClass? SuperClass = this.SuperClass;
					if (SuperClass != null)
					{
						this.ClassFlags |= SuperClass.ClassFlags & EClassFlags.ScriptInherit;
						if (!SuperClass.ClassFlags.HasAnyFlags(EClassFlags.Native))
						{
							throw new UhtException(this, $"Native classes cannot extend non-native classes");
						}
						this.ClassWithin = SuperClass.ClassWithin;
					}
					else
					{
						this.ClassWithin = this.Session.UObject;
					}
					break;
			}
		}

		protected override bool ResolveSelf(UhtResolvePhase ResolvePhase)
		{
			bool bResult = base.ResolveSelf(ResolvePhase);

			switch (ResolvePhase)
			{
				case UhtResolvePhase.InvalidCheck:
					{
						string NativeInterfaceName = "I" + this.EngineName;
						UhtType? NativeInterface = this.Session.FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, NativeInterfaceName);
						if (NativeInterface == null)
						{
							this.LogError($"UInterface '{this.SourceName}' parsed without a corresponding '{NativeInterfaceName}'");
						}
						else
						{
							// Copy the children
							this.AddChildren(NativeInterface.DetchChildren());
						}
					}
					break;
			}

			return bResult;
		}
		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Global)]
		private static UhtParseResult UINTERFACEKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return ParseUInterface(TopScope, ref Token);
		}

		[UhtKeyword(Extends = UhtTableNames.Interface)]
		[UhtKeyword(Extends = UhtTableNames.Interface, Keyword = "GENERATED_BODY")]
		private static UhtParseResult GENERATED_UINTERFACE_BODYKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			UhtClass Class = (UhtClass)TopScope.ScopeType;

			UhtParserHelpers.ParseCompileVersionDeclaration(TopScope.TokenReader, Class);

			Class.GeneratedBodyAccessSpecifier = TopScope.AccessSpecifier;
			Class.GeneratedBodyLineNumber = TopScope.TokenReader.InputLine;

			if (Token.IsValue("GENERATED_UINTERFACE_BODY"))
			{
				TopScope.AccessSpecifier = UhtAccessSpecifier.Public;
			}
			return UhtParseResult.Handled;
		}
		#endregion

		public static UhtParseResult ParseUInterface(UhtParsingScope ParentScope, ref UhtToken Token)
		{
			UhtInterfaceClassParser Class = new UhtInterfaceClassParser(ParentScope.ScopeType, Token.InputLine);
			Class.ClassType = UhtClassType.Interface;

			using (var TopScope = new UhtParsingScope(ParentScope, Class, KeywordTable, UhtAccessSpecifier.Private))
			{
				const string ScopeName = "interface";

				using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, ScopeName))
				{
					// Parse the specifiers
					UhtSpecifierContext SpecifierContext = new UhtSpecifierContext(TopScope, TopScope.TokenReader, Class.MetaData);
					UhtSpecifierParser Specifiers = TopScope.HeaderParser.GetSpecifierParser(SpecifierContext, ScopeName, SpecifierTable);
					Specifiers.ParseSpecifiers();
					Class.PrologLineNumber = TopScope.TokenReader.InputLine;
					Class.ClassFlags |= EClassFlags.Native | EClassFlags.Interface | EClassFlags.Abstract;
					Class.InternalObjectFlags |= EInternalObjectFlags.Native;

					// Consume the class specifier
					TopScope.TokenReader.Require("class");

					// Get the optional API macro
					UhtToken APIMacroToken;
					TopScope.TokenReader.TryOptionalAPIMacro(out APIMacroToken);

					// Get the name of the interface
					Class.SourceName = TopScope.TokenReader.GetIdentifier().Value.ToString();

					// Parse the inheritance
					TopScope.TokenReader.OptionalInheritance((ref UhtToken Identifier) =>
					{
						Class.SuperIdentifier = Identifier;
					});

					// Split the source name into the different parts
					UhtEngineNameParts NameParts = UhtUtilities.GetEngineNameParts(Class.SourceName);
					Class.EngineName = NameParts.EngineName.ToString();

					// Interfaces must start with a valid prefix
					if (NameParts.Prefix != "U")
					{
						TopScope.TokenReader.LogError($"Interface name '{Class.SourceName}' is invalid, the first class should be identified as 'U{NameParts.EngineName}'");
					}

					// Check for an empty engine name
					if (Class.EngineName.Length == 0)
					{
						TopScope.TokenReader.LogError($"When compiling class definition for '{Class.SourceName}', attempting to strip prefix results in an empty name. Did you leave off a prefix?");
					}

					if (APIMacroToken)
					{
						Class.ClassFlags |= EClassFlags.RequiredAPI;
					}

					Specifiers.ParseDeferred();

					if (Class.Outer != null)
					{
						Class.Outer.AddChild(Class);
					}

					TopScope.AddModuleRelativePathToMetaData();

					TopScope.HeaderParser.ParseStatements('{', '}', true);

					TopScope.TokenReader.Require(';');

					if (Class.GeneratedBodyLineNumber == -1)
					{
						TopScope.TokenReader.LogError("Expected a GENERATED_BODY() at the start of the interface");
					}
				}
			}

			return UhtParseResult.Handled;
		}
	}
}
