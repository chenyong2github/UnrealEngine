// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.UHT.Parsers
{
	/** 
	 *	AdvancedDisplay can be used in two ways:
	 *	1. 'AdvancedDisplay = "3"' - the number tells how many parameters (from beginning) should NOT BE marked
	 *	2. 'AdvancedDisplay = "AttachPointName, Location, LocationType"' - list the parameters, that should BE marked
	 */
	struct UhtAdvancedDisplayParameterHandler
	{
		private UhtMetaData MetaData;
		private string[]? ParameterNames;
		private int NumberLeaveUnmarked;
		private int AlreadyLeft;
		private bool bUseNumber;

		public UhtAdvancedDisplayParameterHandler(UhtMetaData MetaData)
		{
			this.MetaData = MetaData;
			this.ParameterNames = null;
			this.NumberLeaveUnmarked = -1;
			this.AlreadyLeft = 0;
			this.bUseNumber = false;

			string? FoundString;
			if (this.MetaData.TryGetValue(UhtNames.AdvancedDisplay, out FoundString))
			{
				this.ParameterNames = FoundString.ToString().Split(',', StringSplitOptions.RemoveEmptyEntries);
				for (int Index = 0, EndIndex = this.ParameterNames.Length; Index < EndIndex; ++Index)
				{
					this.ParameterNames[Index] = this.ParameterNames[Index].Trim();
				}
				if (this.ParameterNames.Length == 1)
				{
					bUseNumber = int.TryParse(this.ParameterNames[0], out NumberLeaveUnmarked);
				}
			}
		}

		/** 
		 * return if given parameter should be marked as Advance View, 
		 * the function should be called only once for any parameter
		 */
		public bool ShouldMarkParameter(StringView ParameterName)
		{
			if (this.bUseNumber)
			{
				if (this.NumberLeaveUnmarked < 0)
				{
					return false;
				}
				if (this.AlreadyLeft < this.NumberLeaveUnmarked)
				{
					++this.AlreadyLeft;
					return false;
				}
				return true;
			}

			if (this.ParameterNames == null)
			{
				return false;
			}

			foreach (string Element in this.ParameterNames)
			{
				if (ParameterName.Span.Equals(Element, StringComparison.OrdinalIgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		/** return if more parameters can be marked */
		public bool CanMarkMore()
		{
			return this.bUseNumber ? this.NumberLeaveUnmarked > 0 : (this.ParameterNames != null && this.ParameterNames.Length > 0);
		}
	}

	[UnrealHeaderTool]
	public class UhtFunctionParser : UhtFunction
	{
		private static UhtKeywordTable KeywordTable = UhtKeywordTables.Instance.Get(UhtTableNames.Function);
		private static UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.Function);

		public UhtFunctionParser(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		/// <summary>
		/// True if the function specifier has a getter/setter specified
		/// </summary>
		public bool bSawPropertyAccessor = false;

		protected override bool ResolveSelf(UhtResolvePhase ResolvePhase)
		{
			bool bResult = base.ResolveSelf(ResolvePhase);
			switch (ResolvePhase)
			{
				case UhtResolvePhase.Properties:
					UhtPropertyParser.ResolveChildren(this, GetPropertyParseOptions(false));
					foreach (UhtProperty Property in this.Properties)
					{
						if (Property.DefaultValueTokens != null)
						{
							string Key = "CPP_Default_" + Property.EngineName;
							if (!this.MetaData.ContainsKey(Key))
							{
								bool bParsed = false;
								try
								{
									// All tokens MUST be consumed from the reader
									StringBuilder Builder = new StringBuilder();
									IUhtTokenReader DefaultValueReader = UhtTokenReplayReader.GetThreadInstance(Property, this.HeaderFile.Data.Memory, Property.DefaultValueTokens.ToArray(), UhtTokenType.EndOfDefault);
									bParsed = Property.SanitizeDefaultValue(DefaultValueReader, Builder) && DefaultValueReader.bIsEOF;
									if (bParsed)
									{
										this.MetaData.Add(Key, Builder.ToString());
									}
								}
								catch (Exception)
								{
									// Ignore the exception for now
								}

								if (!bParsed)
								{
									StringView DefaultValueText = new StringView(this.HeaderFile.Data, Property.DefaultValueTokens.First().InputStartPos,
										Property.DefaultValueTokens.Last().InputEndPos - Property.DefaultValueTokens.First().InputStartPos);
									Property.LogError($"C++ Default parameter not parsed: {Property.SourceName} '{DefaultValueText}'");
								}
							}
						}
					}
					break;
			}
			return bResult;
		}

		public UhtPropertyParseOptions GetPropertyParseOptions(bool bReturnValue)
		{
			switch (this.FunctionType)
			{
				case UhtFunctionType.Delegate:
				case UhtFunctionType.SparseDelegate:
					return (bReturnValue ? UhtPropertyParseOptions.None : UhtPropertyParseOptions.CommaSeparatedName) | UhtPropertyParseOptions.DontAddReturn;

				case UhtFunctionType.Function:
					UhtPropertyParseOptions Options = UhtPropertyParseOptions.DontAddReturn; // Fetch the function name
					Options |= bReturnValue ? UhtPropertyParseOptions.FunctionNameIncluded : UhtPropertyParseOptions.NameIncluded;
					if (this.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native))
					{
						Options |= UhtPropertyParseOptions.NoAutoConst;
					}
					return Options;

				default:
					throw new UhtIceException("Unknown enumeration value");
			}
		}
		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Global)]
		[UhtKeyword(Extends = UhtTableNames.Class)]
		private static UhtParseResult UDELEGATEKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return ParseUDelegate(TopScope, Token, true);
		}

		[UhtKeyword(Extends = UhtTableNames.Class)]
		[UhtKeyword(Extends = UhtTableNames.Interface)]
		[UhtKeyword(Extends = UhtTableNames.NativeInterface)]
		private static UhtParseResult UFUNCTIONKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return ParseUFunction(TopScope, Token);
		}

		[UhtKeywordCatchAll(Extends = UhtTableNames.Global)]
		private static UhtParseResult ParseCatchAllKeyword(UhtParsingScope TopScope, ref UhtToken Token)
		{
			if (UhtFunctionParser.IsValidateDelegateDeclaration(Token))
			{
				return ParseUDelegate(TopScope, Token, false);
			}
			return UhtParseResult.Unhandled;
		}
		#endregion

		private static UhtParseResult ParseUDelegate(UhtParsingScope ParentScope, UhtToken Token, bool bHasSpecifiers)
		{
			UhtFunctionParser Function = new UhtFunctionParser(ParentScope.ScopeType, Token.InputLine);

			using (var TopScope = new UhtParsingScope(ParentScope, Function, KeywordTable, UhtAccessSpecifier.Public))
			{
				const string ScopeName = "delegate declaration";

				using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, ScopeName))
				{
					TopScope.AddModuleRelativePathToMetaData();

					UhtSpecifierContext SpecifierContext = new UhtSpecifierContext(TopScope, TopScope.TokenReader, Function.MetaData);
					UhtSpecifierParser Specifiers = TopScope.HeaderParser.GetSpecifierParser(SpecifierContext, ScopeName, SpecifierTable);

					// If this is a UDELEGATE, parse the specifiers
					StringView DelegateMacro = new StringView();
					if (bHasSpecifiers)
					{
						Specifiers.ParseSpecifiers();
						Specifiers.ParseDeferred();
						FinalizeFunctionSpecifiers(Function);

						UhtToken MacroToken = TopScope.TokenReader.GetToken();
						if (!IsValidateDelegateDeclaration(MacroToken))
						{
							throw new UhtTokenException(TopScope.TokenReader, MacroToken, "delegate macro");
						}
						DelegateMacro = MacroToken.Value;
					}
					else
					{
						DelegateMacro = Token.Value;
					}

					// Break the delegate declaration macro down into parts
					bool bHasReturnValue = DelegateMacro.Span.Contains("_RetVal".AsSpan(), StringComparison.Ordinal);
					bool bDeclaredConst = DelegateMacro.Span.Contains("_Const".AsSpan(), StringComparison.Ordinal);
					bool bIsMulticast = DelegateMacro.Span.Contains("_MULTICAST".AsSpan(), StringComparison.Ordinal);
					bool bIsSparse = DelegateMacro.Span.Contains("_SPARSE".AsSpan(), StringComparison.Ordinal);

					// Determine the parameter count
					int FoundParamIndex = UhtConfig.Instance.FindDelegateParameterCount(DelegateMacro);

					// Try reconstructing the string to make sure it matches our expectations
					string ExpectedOriginalString = string.Format("DECLARE_DYNAMIC{0}{1}_DELEGATE{2}{3}{4}",
						bIsMulticast ? "_MULTICAST" : "",
						bIsSparse ? "_SPARSE" : "",
						bHasReturnValue ? "_RetVal" : "",
						UhtConfig.Instance.GetDelegateParameterCountString(FoundParamIndex),
						bDeclaredConst ? "_Const" : "");
					if (DelegateMacro != ExpectedOriginalString)
					{
						throw new UhtException(TopScope.TokenReader, $"Unable to parse delegate declaration; expected '{ExpectedOriginalString}' but found '{DelegateMacro}'.");
					}

					// Multi-cast delegate function signatures are not allowed to have a return value
					if (bHasReturnValue && bIsMulticast)
					{
						throw new UhtException(TopScope.TokenReader, "Multi-cast delegates function signatures must not return a value");
					}

					// Delegate signature
					Function.FunctionType = bIsSparse ? UhtFunctionType.SparseDelegate : UhtFunctionType.Delegate;
					Function.FunctionFlags |= EFunctionFlags.Public | EFunctionFlags.Delegate;
					if (bIsMulticast)
					{
						Function.FunctionFlags |= EFunctionFlags.MulticastDelegate;
					}

					// Now parse the macro body
					TopScope.TokenReader.Require('(');

					// Parse the return type
					UhtProperty? ReturnValueProperty = null;
					if (bHasReturnValue)
					{
						TopScope.HeaderParser.GetPropertyParser(TopScope).Parse(EPropertyFlags.None,
							Function.GetPropertyParseOptions(true), UhtParsePropertyDeclarationStyle.None, UhtPropertyCategory.Return,
							(UhtParsingScope TopScope, UhtProperty Property, ref UhtToken NameToken, UhtLayoutMacroType LayoutMacroType) =>
							{
								Property.PropertyFlags |= EPropertyFlags.Parm | EPropertyFlags.OutParm | EPropertyFlags.ReturnParm;
								ReturnValueProperty = Property;
							});
						TopScope.TokenReader.Require(',');
					}

					// Skip white spaces to get InputPos exactly on beginning of function name.
					TopScope.TokenReader.SkipWhitespaceAndComments();

					// Get the delegate name
					UhtToken FuncNameToken = TopScope.TokenReader.GetIdentifier("name");
					Function.SourceName = FuncNameToken.Value.ToString();

					// If this is a delegate function then go ahead and mangle the name so we don't collide with
					// actual functions or properties
					{
						//@TODO: UCREMOVAL: Eventually this mangling shouldn't occur

						// Remove the leading F
						if (Function.SourceName[0] != 'F')
						{
							TopScope.TokenReader.LogError("Delegate type declarations must start with F");
						}
						Function.StrippedFunctionName = Function.SourceName.Substring(1);
						Function.EngineName = $"{Function.StrippedFunctionName}{UhtFunction.GeneratedDelegateSignatureSuffix}";
					}

					SetFunctionNames(Function);
					AddFunction(Function);

					// determine whether this function should be 'const'
					if (bDeclaredConst)
					{
						Function.FunctionFlags |= EFunctionFlags.Const;
						Function.FunctionExportFlags |= UhtFunctionExportFlags.DeclaredConst;
					}

					if (bIsSparse)
					{
						TopScope.TokenReader.Require(',');
						UhtToken Name = TopScope.TokenReader.GetIdentifier("OwningClass specifier");
						UhtEngineNameParts Parts = UhtUtilities.GetEngineNameParts(Name.Value);
						Function.SparseOwningClassName = Parts.EngineName.ToString();
						TopScope.TokenReader.Require(',');
						Function.SparseDelegateName = TopScope.TokenReader.GetIdentifier("delegate name").Value.ToString();
					}

					Function.FunctionLineNumber = TopScope.TokenReader.InputLine;

					// Get parameter list.
					if (FoundParamIndex >= 0)
					{
						TopScope.TokenReader.Require(',');

						ParseParameterList(TopScope, Function.GetPropertyParseOptions(false));
					}
					else
					{
						// Require the closing paren even with no parameter list
						TopScope.TokenReader.Require(')');
					}

					// Add back in the return value
					if (ReturnValueProperty != null)
					{
						TopScope.ScopeType.AddChild(ReturnValueProperty);
					}

					// Verify the number of parameters (FoundParamIndex = -1 means zero parameters, 0 means one, ...)
					int ExpectedProperties = FoundParamIndex + 1 + (bHasReturnValue ? 1 : 0);
					int PropertiesCount = Function.Properties.Count();
					if (PropertiesCount != ExpectedProperties)
					{
						throw new UhtException(TopScope.TokenReader, $"Expected {ExpectedProperties} parameters but found {PropertiesCount} parameters");
					}

					// The macro line must be set here
					Function.MacroLineNumber = TopScope.TokenReader.InputLine;

					// Try parsing metadata for the function
					Specifiers.ParseFieldMetaData(Function.SourceName);

					TopScope.AddFormattedCommentsAsTooltipMetaData();

					// Optionally consume a semicolon, it's not required for the delegate macro since it contains one internally
					TopScope.TokenReader.Optional(';');
				}
				return UhtParseResult.Handled;
			}
		}

		private static UhtParseResult ParseUFunction(UhtParsingScope ParentScope, UhtToken Token)
		{
			UhtFunctionParser Function = new UhtFunctionParser(ParentScope.ScopeType, Token.InputLine);

			using (var TopScope = new UhtParsingScope(ParentScope, Function, KeywordTable, UhtAccessSpecifier.Public))
			{
				UhtParsingScope OuterClassScope = TopScope.CurrentClassScope;
				UhtClass OuterClass = (UhtClass)OuterClassScope.ScopeType;
				string ScopeName = "function";

				using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, ScopeName))
				{
					TopScope.AddModuleRelativePathToMetaData();

					UhtSpecifierContext SpecifierContext = new UhtSpecifierContext(TopScope, TopScope.TokenReader, Function.MetaData);
					UhtSpecifierParser SpecifierParser = TopScope.HeaderParser.GetSpecifierParser(SpecifierContext, ScopeName, SpecifierTable);
					SpecifierParser.ParseSpecifiers();

					if (!OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Native))
					{
						throw new UhtException(Function, "Should only be here for native classes!");
					}

					Function.MacroLineNumber = TopScope.TokenReader.InputLine;
					Function.FunctionFlags |= EFunctionFlags.Native;

					bool bAutomaticallyFinal = true;
					switch (OuterClassScope.AccessSpecifier)
					{
						case UhtAccessSpecifier.Public:
							Function.FunctionFlags |= EFunctionFlags.Public;
							break;

						case UhtAccessSpecifier.Protected:
							Function.FunctionFlags |= EFunctionFlags.Protected;
							break;

						case UhtAccessSpecifier.Private:
							Function.FunctionFlags |= EFunctionFlags.Private | EFunctionFlags.Final;

							// This is automatically final as well, but in a different way and for a different reason
							bAutomaticallyFinal = false;
							break;
					}

					if (TopScope.TokenReader.TryOptional("static"))
					{
						Function.FunctionFlags |= EFunctionFlags.Static;
						Function.FunctionExportFlags |= UhtFunctionExportFlags.CppStatic;
					}

					if (Function.MetaData.ContainsKey(UhtNames.CppFromBpEvent))
					{
						Function.FunctionFlags |= EFunctionFlags.Event;
					}

					if ((TopScope.HeaderParser.GetCurrentCompositeCompilerDirective() & UhtCompilerDirective.WithEditor) != 0)
					{
						Function.FunctionFlags |= EFunctionFlags.EditorOnly;
					}

					SpecifierParser.ParseDeferred();
					FinalizeFunctionSpecifiers(Function);

					if (Function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CustomThunk) && !Function.MetaData.ContainsKey(UhtNames.CustomThunk))
					{
						Function.MetaData.Add(UhtNames.CustomThunk, true);
					}

					if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
					{
						// Network replicated functions are always events, and are only final if sealed
						ScopeName = "event";
						TokenContext.Reset(ScopeName);
						bAutomaticallyFinal = false;
					}

					if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
					{
						ScopeName = Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native) ? "BlueprintNativeEvent" : "BlueprintImplementableEvent";
						TokenContext.Reset(ScopeName);
						bAutomaticallyFinal = false;
					}

					// Record the tokens so we can detect this function as a declaration later (i.e. RPC)
					using (UhtTokenRecorder TokenRecorder = new UhtTokenRecorder(ParentScope))
					{

						if (TopScope.TokenReader.TryOptional("virtual"))
						{
							Function.FunctionExportFlags |= UhtFunctionExportFlags.Virtual;
						}

						bool bInternalOnly = Function.MetaData.GetBoolean(UhtNames.BlueprintInternalUseOnly);

						// Peek ahead to look for a CORE_API style DLL import/export token if present
						UhtToken APIMacroToken;
						if (TopScope.TokenReader.TryOptionalAPIMacro(out APIMacroToken))
						{
							//@TODO: Validate the module name for RequiredAPIMacroIfPresent
							Function.FunctionFlags |= EFunctionFlags.RequiredAPI;
							Function.FunctionExportFlags |= UhtFunctionExportFlags.RequiredAPI;
						}

						// Look for static again, in case there was an ENGINE_API token first
						if (APIMacroToken && TopScope.TokenReader.TryOptional("static"))
						{
							TopScope.TokenReader.LogError($"Unexpected API macro '{APIMacroToken.Value}'. Did you mean to put '{APIMacroToken.Value}' after the static keyword?");
						}

						// Look for virtual again, in case there was an ENGINE_API token first
						if (TopScope.TokenReader.TryOptional("virtual"))
						{
							Function.FunctionExportFlags |= UhtFunctionExportFlags.Virtual;
						}

						// If virtual, remove the implicit final, the user can still specifying an explicit final at the end of the declaration
						if (Function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.Virtual))
						{
							bAutomaticallyFinal = false;
						}

						// Handle the initial implicit/explicit final
						// A user can still specify an explicit final after the parameter list as well.
						if (bAutomaticallyFinal || Function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.SealedEvent))
						{
							Function.FunctionFlags |= EFunctionFlags.Final;
							Function.FunctionExportFlags |= UhtFunctionExportFlags.Final | UhtFunctionExportFlags.AutoFinal;
						}

						// Get return type.  C++ style functions always have a return value type, even if it's void
						UhtToken FuncNameToken = new UhtToken();
						UhtProperty? ReturnValueProperty = null;
						TopScope.HeaderParser.GetPropertyParser(TopScope).Parse(EPropertyFlags.None,
							Function.GetPropertyParseOptions(true), UhtParsePropertyDeclarationStyle.None, UhtPropertyCategory.Return,
							(UhtParsingScope TopScope, UhtProperty Property, ref UhtToken NameToken, UhtLayoutMacroType LayoutMacroType) =>
							{
								Property.PropertyFlags |= EPropertyFlags.Parm | EPropertyFlags.OutParm | EPropertyFlags.ReturnParm;
								FuncNameToken = NameToken;
								if (!(Property is UhtVoidProperty))
								{
									ReturnValueProperty = Property;
								}
							});

						if (FuncNameToken.Value.Length == 0)
						{
							throw new UhtException(TopScope.TokenReader, "expected return value and function name");
						}

						// Get function or operator name.
						Function.SourceName = FuncNameToken.Value.ToString();

						ScopeName = $"{ScopeName} '{Function.SourceName}'";
						TokenContext.Reset(ScopeName);

						TopScope.TokenReader.Require('(');

						SetFunctionNames(Function);
						AddFunction(Function);

						// Get parameter list.
						ParseParameterList(TopScope, Function.GetPropertyParseOptions(false));

						// Add back in the return value
						if (ReturnValueProperty != null)
						{
							TopScope.ScopeType.AddChild(ReturnValueProperty);
						}

						// determine whether this function should be 'const'
						if (TopScope.TokenReader.TryOptional("const"))
						{
							if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
							{
								// @TODO: UCREMOVAL Reconsider?
								//Throwf(TEXT("'const' may only be used for native functions"));
							}

							Function.FunctionFlags |= EFunctionFlags.Const;
							Function.FunctionExportFlags |= UhtFunctionExportFlags.DeclaredConst;
						}

						// Try parsing metadata for the function
						SpecifierParser.ParseFieldMetaData(Function.EngineName);

						TopScope.AddFormattedCommentsAsTooltipMetaData();

						// 'final' and 'override' can appear in any order before an optional '= 0' pure virtual specifier
						bool bFoundFinal = TopScope.TokenReader.TryOptional("final");
						bool bFoundOverride = TopScope.TokenReader.TryOptional("override");
						if (!bFoundFinal && bFoundOverride)
						{
							bFoundFinal = TopScope.TokenReader.TryOptional("final");
						}

						// Handle C++ style functions being declared as abstract
						if (TopScope.TokenReader.TryOptional('='))
						{
							int ZeroValue = 1;
							bool bGotZero = TopScope.TokenReader.TryOptionalConstInt(out ZeroValue);
							bGotZero = bGotZero && (ZeroValue == 0);
							if (!bGotZero || ZeroValue != 0)
							{
								throw new UhtException(TopScope.TokenReader, "Expected 0 to indicate function is abstract");
							}
						}

						// Look for the final keyword to indicate this function is sealed
						if (bFoundFinal)
						{
							// This is a final (prebinding, non-overridable) function
							Function.FunctionFlags |= EFunctionFlags.Final;
							Function.FunctionExportFlags |= UhtFunctionExportFlags.Final;
						}

						// Optionally consume a semicolon
						// This is optional to allow inline function definitions
						if (TopScope.TokenReader.TryOptional(';'))
						{
							// Do nothing (consume it)
						}
						else if (TopScope.TokenReader.TryPeekOptional('{'))
						{
							// Skip inline function bodies
							UhtToken TokenCopy = new UhtToken();
							TopScope.TokenReader.SkipDeclaration(ref TokenCopy);
						}
					}
				}
				return UhtParseResult.Handled;
			}
		}

		private static void FinalizeFunctionSpecifiers(UhtFunction Function)
		{
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				// Network replicated functions are always events
				Function.FunctionFlags |= EFunctionFlags.Event;
			}
		}

		internal static bool IsValidateDelegateDeclaration(UhtToken Token)
		{
			return (Token.IsIdentifier() && Token.Value.Span.StartsWith("DECLARE_DYNAMIC_"));
		}

		private static void ParseParameterList(UhtParsingScope TopScope, UhtPropertyParseOptions Options)
		{
			UhtFunction Function = (UhtFunction)TopScope.ScopeType;

			bool bIsNetFunc = Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net);
			UhtPropertyCategory PropertyCategory = bIsNetFunc ? UhtPropertyCategory.ReplicatedParameter : UhtPropertyCategory.RegularParameter;
			EPropertyFlags DisallowFlags = ~(EPropertyFlags.ParmFlags | EPropertyFlags.AutoWeak | EPropertyFlags.RepSkip | EPropertyFlags.UObjectWrapper | EPropertyFlags.NativeAccessSpecifiers);

			UhtAdvancedDisplayParameterHandler AdvancedDisplay = new UhtAdvancedDisplayParameterHandler(TopScope.ScopeType.MetaData);

			TopScope.TokenReader.RequireList(')', ',', false, () =>
			{
				TopScope.HeaderParser.GetPropertyParser(TopScope).Parse(DisallowFlags, Options, UhtParsePropertyDeclarationStyle.None, PropertyCategory,
					(UhtParsingScope TopScope, UhtProperty Property, ref UhtToken NameToken, UhtLayoutMacroType LayoutMacroType) =>
					{
						Property.PropertyFlags |= EPropertyFlags.Parm;
						if (AdvancedDisplay.CanMarkMore() && AdvancedDisplay.ShouldMarkParameter(Property.EngineName))
						{
							Property.PropertyFlags |= EPropertyFlags.AdvancedDisplay;
						}

						// Default value.
						if (TopScope.TokenReader.TryOptional('='))
						{
							List<UhtToken> DefaultValueTokens = new List<UhtToken>();
							int ParenthesisNestCount = 0;
							while (!TopScope.TokenReader.bIsEOF)
							{
								UhtToken Token = TopScope.TokenReader.PeekToken();
								if (Token.IsSymbol(','))
								{
									if (ParenthesisNestCount == 0)
									{
										break;
									}
									DefaultValueTokens.Add(Token);
									TopScope.TokenReader.ConsumeToken();
								}
								else if (Token.IsSymbol(')'))
								{
									if (ParenthesisNestCount == 0)
									{
										break;
									}
									DefaultValueTokens.Add(Token);
									TopScope.TokenReader.ConsumeToken();
									--ParenthesisNestCount;
								}
								else if (Token.IsSymbol('('))
								{
									++ParenthesisNestCount;
									DefaultValueTokens.Add(Token);
									TopScope.TokenReader.ConsumeToken();
								}
								else
								{
									DefaultValueTokens.Add(Token);
									TopScope.TokenReader.ConsumeToken();
								}
							}

							// allow exec functions to be added to the metaData, this is so we can have default params for them.
							bool bStoreCppDefaultValueInMetaData = Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec);
							if (DefaultValueTokens.Count > 0 && bStoreCppDefaultValueInMetaData)
							{
								Property.DefaultValueTokens = DefaultValueTokens;
							}
						}
					});
			});
		}

		private static void AddFunction(UhtFunction Function)
		{
			if (Function.Outer != null)
			{
				Function.Outer.AddChild(Function);
			}
		}

		private static void SetFunctionNames(UhtFunction Function)
		{
			// The source name won't have the suffix applied to delegate names, however, the engine name will
			// We use the engine name because we need to detect the suffix for delegates
			string FunctionName = Function.EngineName;
			if (FunctionName.EndsWith(UhtFunction.GeneratedDelegateSignatureSuffix))
			{
				FunctionName = FunctionName.Substring(0, FunctionName.Length - UhtFunction.GeneratedDelegateSignatureSuffix.Length);
			}

			Function.UnMarshalAndCallName = "exec" + FunctionName;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				Function.MarshalAndCallName = FunctionName;
				if (Function.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native))
				{
					Function.CppImplName = Function.EngineName + "_Implementation";
				}
			}
			else if (Function.FunctionFlags.HasAllFlags(EFunctionFlags.Native | EFunctionFlags.Net))
			{
				Function.MarshalAndCallName = FunctionName;
				if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetResponse))
				{
					// Response function implemented by programmer and called directly from thunk
					Function.CppImplName = Function.EngineName;
				}
				else
				{
					if (Function.CppImplName.Length == 0)
					{
						Function.CppImplName = Function.EngineName + "_Implementation";
					}
					else if (Function.CppImplName == FunctionName)
					{
						Function.LogError("Native implementation function must be different than original function name.");
					}

					if (Function.CppValidationImplName.Length == 0 && Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
					{
						Function.CppValidationImplName = Function.EngineName + "_Validate";
					}
					else if (Function.CppValidationImplName == FunctionName)
					{
						Function.LogError("Validation function must be different than original function name.");
					}
				}
			}
			else if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
			{
				Function.MarshalAndCallName = "delegate" + FunctionName;
			}

			if (Function.CppImplName.Length == 0)
			{
				Function.CppImplName = FunctionName;
			}

			if (Function.MarshalAndCallName.Length == 0)
			{
				Function.MarshalAndCallName = "event" + FunctionName;
			}
		}
	}
}
