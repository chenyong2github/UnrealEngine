// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public class UhtParserNativeInterfaceClass : UhtClassBaseParser
	{
		private static UhtKeywordTable KeywordTable = UhtKeywordTables.Instance.Get(UhtTableNames.NativeInterface);
		private static UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.NativeInterface);

		public UhtParserNativeInterfaceClass(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		protected override bool ResolveSelf(UhtResolvePhase ResolvePhase)
		{
			bool bResult = base.ResolveSelf(ResolvePhase);

			switch (ResolvePhase)
			{
				case UhtResolvePhase.InvalidCheck:
					string InterfaceName = "U" + this.EngineName;
					UhtClass? Interface = (UhtClass?) this.Session.FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, InterfaceName);
					if (Interface == null)
					{
						if (this.bHasGeneratedBody || this.Children.Count != 0)
						{
							this.LogError($"Native interface '{this.SourceName}' parsed without a corresponding '{InterfaceName}'");
						}
						else
						{
							this.bVisibleType = false;
							bResult = false;
						}
					}
					else
					{
						this.AlternateObject = Interface;
						Interface.NativeInterface = this;
						//COMPATIBILITY-TODO - Use the native interface access specifier
						Interface.GeneratedBodyAccessSpecifier = this.GeneratedBodyAccessSpecifier;

						if (this.GeneratedBodyLineNumber == -1)
						{
							this.LogError("Expected a GENERATED_BODY() at the start of the native interface");
						}
					}
					break;
			}

			return bResult;
		}

		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Global, Keyword = "class", DisableUsageError = true)]
		private static UhtParseResult ClassKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			using (var SaveState = new UhtTokenSaveState(TopScope.TokenReader))
			{
				UhtToken SourceName = new UhtToken();
				UhtToken SuperName = new UhtToken();
				if (TryParseIInterface(TopScope, ref Token, out SourceName, out SuperName))
				{
					SaveState.AbandonState();
					ParseIInterface(TopScope, ref Token, SourceName, SuperName);
					return UhtParseResult.Handled;
				}
			}
			return TopScope.TokenReader.SkipDeclaration(ref Token) ? UhtParseResult.Handled : UhtParseResult.Invalid;
		}

		[UhtKeyword(Extends = UhtTableNames.NativeInterface)]
		[UhtKeyword(Extends = UhtTableNames.NativeInterface, Keyword = "GENERATED_BODY")]
		private static UhtParseResult GENERATED_IINTERFACE_BODYKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			UhtClass Class = (UhtClass)TopScope.ScopeType;

			UhtParserHelpers.ParseCompileVersionDeclaration(TopScope.TokenReader, Class);

			Class.GeneratedBodyAccessSpecifier = TopScope.AccessSpecifier;
			Class.GeneratedBodyLineNumber = TopScope.TokenReader.InputLine;
			Class.bHasGeneratedBody = true;

			if (Token.IsValue("GENERATED_IINTERFACE_BODY"))
			{
				TopScope.AccessSpecifier = UhtAccessSpecifier.Public;
			}
			return UhtParseResult.Handled;
		}
		#endregion

		private static bool TryParseIInterface(UhtParsingScope ParentScope, ref UhtToken Token, out UhtToken SourceName, out UhtToken SuperName)
		{
			IUhtTokenReader TokenReader = ParentScope.TokenReader;

			// Get the optional API macro
			UhtToken APIMacroToken;
			TokenReader.TryOptionalAPIMacro(out APIMacroToken);

			// Get the name of the interface
			SourceName = TokenReader.GetIdentifier();

			// Old UHT would still parse the inheritance 
			SuperName = new UhtToken();
			if (TokenReader.TryOptional(':'))
			{
				if (!TokenReader.TryOptional("public"))
				{
					return false;
				}
				SuperName = TokenReader.GetIdentifier();
			}

			// Only process classes starting with 'I'
			if (SourceName.Value.Span[0] != 'I')
			{
				return false;
			}

			// If we end with a ';', then this is a forward declaration
			if (TokenReader.TryOptional(';'))
			{
				return false;
			}

			// If we don't have a '{', then this is something else
			if (!TokenReader.TryPeekOptional('{'))
			{
				return false;
			}
			return true;
		}

		private static void ParseIInterface(UhtParsingScope ParentScope, ref UhtToken Token, UhtToken SourceName, UhtToken SuperName)
		{
			IUhtTokenReader TokenReader = ParentScope.TokenReader;

			UhtParserNativeInterfaceClass Class = new UhtParserNativeInterfaceClass(ParentScope.ScopeType, Token.InputLine);
			Class.ClassType = UhtClassType.NativeInterface;
			Class.SourceName = SourceName.Value.ToString();
			Class.ClassFlags |= EClassFlags.Native | EClassFlags.Interface;

			// Split the source name into the different parts
			UhtEngineNameParts NameParts = UhtUtilities.GetEngineNameParts(Class.SourceName);
			Class.EngineName = NameParts.EngineName.ToString();

			// Check for an empty engine name
			if (Class.EngineName.Length == 0)
			{
				TokenReader.LogError($"When compiling class definition for '{Class.SourceName}', attempting to strip prefix results in an empty name. Did you leave off a prefix?");
			}

			if (Class.Outer != null)
			{
				Class.Outer.AddChild(Class);
			}

			Class.SuperIdentifier = SuperName;

			using (var TopScope = new UhtParsingScope(ParentScope, Class, KeywordTable, UhtAccessSpecifier.Private))
			{
				const string ScopeName = "native interface";

				using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, ScopeName))
				{
					TopScope.HeaderParser.ParseStatements('{', '}', false);
					TokenReader.Require(';');
				}
			}
			return;
		}
	}
}
