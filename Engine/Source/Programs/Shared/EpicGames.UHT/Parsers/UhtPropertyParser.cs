// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;

namespace EpicGames.UHT.Parsers
{
	/// <summary>
	/// Options that customize the parsing of properties.
	/// </summary>
	[Flags]
	public enum UhtPropertyParseOptions
	{
		None = 0,

		/// <summary>
		/// Don't automatically mark properties as CPF_Const
		/// </summary>
		NoAutoConst = 1 << 0,

		/// <summary>
		/// Parse for the layout macro
		/// </summary>
		ParseLayoutMacro = 1 << 1,

		/// <summary>
		/// If set, then the name of the property will be parsed with the type
		/// </summary>
		FunctionNameIncluded = 1 << 2,

		/// <summary>
		/// If set, then the name of the property will be parsed with the type
		/// </summary>
		NameIncluded = 1 << 3,

		/// <summary>
		/// When parsing delegates, the name is separated by commas
		/// </summary>
		CommaSeparatedName = 1 << 4,

		/// <summary>
		/// Multiple properties can be defined separated by commas
		/// </summary>
		List = 1 << 5,

		/// <summary>
		/// Don't add a return type to the property list (return values go at the end)
		/// </summary>
		DontAddReturn = 1 << 6,

		/// <summary>
		/// If set, add the module relative path to the parameter's meta data
		/// </summary>
		AddModuleRelativePath = 1 << 7,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtParsePropertyOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtPropertyParseOptions InFlags, UhtPropertyParseOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtPropertyParseOptions InFlags, UhtPropertyParseOptions TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtPropertyParseOptions InFlags, UhtPropertyParseOptions TestFlags, UhtPropertyParseOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// The property style of a variable declaration being parsed
	/// </summary>
	public enum UhtParsePropertyDeclarationStyle
	{

		/// <summary>
		/// Property is a function return, parameters or this is a template argument
		/// </summary>
		None,

		/// <summary>
		/// Class or script structure property
		/// </summary>
		UPROPERTY
	};

	public enum UhtLayoutMacroType
	{
		None,
		Array,
		ArrayEditorOnly,
		Bitfield,
		BitfieldEditorOnly,
		Field,
		FieldEditorOnly,
		FieldInitialized,
	}

	public static class UhtLayoutMacroTypeExtensions
	{
		public static bool IsEditorOnly(this UhtLayoutMacroType LayoutMacroType)
		{
			switch (LayoutMacroType)
			{
				case UhtLayoutMacroType.ArrayEditorOnly:
				case UhtLayoutMacroType.BitfieldEditorOnly:
				case UhtLayoutMacroType.FieldEditorOnly:
					return true;

				default:
					return false;
			}
		}

		public static bool IsBitfield(this UhtLayoutMacroType LayoutMacroType)
		{
			switch (LayoutMacroType)
			{
				case UhtLayoutMacroType.Bitfield:
				case UhtLayoutMacroType.BitfieldEditorOnly:
					return true;

				default:
					return false;
			}
		}

		public static bool IsArray(this UhtLayoutMacroType LayoutMacroType)
		{
			switch (LayoutMacroType)
			{
				case UhtLayoutMacroType.Array:
				case UhtLayoutMacroType.ArrayEditorOnly:
					return true;

				default:
					return false;
			}
		}

		public static bool HasInitializer(this UhtLayoutMacroType LayoutMacroType)
		{
			switch (LayoutMacroType)
			{
				case UhtLayoutMacroType.FieldInitialized:
					return true;

				default:
					return false;
			}
		}

		public static StringView MacroName(this UhtLayoutMacroType LayoutMacroType)
		{
			switch (LayoutMacroType)
			{
				default:
				case UhtLayoutMacroType.None:
					throw new UhtIceException("Invalid macro name for ELayoutMacroType");

				case UhtLayoutMacroType.Array:
					return "LAYOUT_ARRAY";

				case UhtLayoutMacroType.ArrayEditorOnly:
					return "LAYOUT_ARRAY_EDITORONLY";

				case UhtLayoutMacroType.Bitfield:
					return "LAYOUT_BITFIELD";

				case UhtLayoutMacroType.BitfieldEditorOnly:
					return "LAYOUT_BITFIELD_EDITORONLY";

				case UhtLayoutMacroType.Field:
					return "LAYOUT_FIELD";

				case UhtLayoutMacroType.FieldEditorOnly:
					return "LAYOUT_FIELD_EDITORONLY";

				case UhtLayoutMacroType.FieldInitialized:
					return "LAYOUT_FIELD_INITIALIZED";
			}
		}

		public static KeyValuePair<StringView, UhtLayoutMacroType> MacroNameAndValue(this UhtLayoutMacroType LayoutMacroType)
		{
			return new KeyValuePair<StringView, UhtLayoutMacroType>(LayoutMacroType.MacroName(), LayoutMacroType);
		}
	}

	public delegate void UhtPropertyDelegate(UhtParsingScope TopScope, UhtProperty Property, ref UhtToken NameToken, UhtLayoutMacroType LayoutMacroType);

	public class UhtPropertySpecifierContext : UhtSpecifierContext
	{
		public UhtPropertySettings PropertySettings;
		public bool bSeenEditSpecifier = false;
		public bool bSeenBlueprintWriteSpecifier = false;
		public bool bSeenBlueprintReadOnlySpecifier = false;
		public bool bSeenBlueprintGetterSpecifier = false;

		public UhtPropertySpecifierContext(UhtParsingScope Scope, IUhtMessageSite MessageSite) : base(Scope, MessageSite, UhtMetaData.Empty)
		{
			this.PropertySettings = new UhtPropertySettings(Scope.ScopeType, 1, UhtPropertyCategory.Member, 0);
		}
	}

	/// <summary>
	/// A parsed property is a property that was parsed but couldn't yet be resolved.  It retains the list of tokens needed
	/// to resolve the type of the property.  It will be replaced with the resolved property type during property resolution.
	/// </summary>
	public class UhtPreResolveProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "UHTParsedProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "invalid"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "invalid"; }

		public ReadOnlyMemory<UhtToken> TypeTokens;
		public UhtPropertySettings PropertySettings;

		public UhtPreResolveProperty(UhtPropertySettings PropertySettings, ReadOnlyMemory<UhtToken> TypeTokens) : base(PropertySettings)
		{
			this.TypeTokens = TypeTokens;
			this.PropertySettings = PropertySettings;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterGet(StringBuilder Builder)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			throw new NotImplementedException();
		}
	}

	public class UhtPropertyParser : IUhtMessageExtraContext
	{
		private UhtPropertySpecifierContext SpecifierContext;
		private UhtPropertyParseOptions Options;
		private UhtParsePropertyDeclarationStyle DeclarationStyle;

		private IUhtTokenReader TokenReader => this.SpecifierContext.Scope.TokenReader;
		private UhtParsingScope TopScope => this.SpecifierContext.Scope;

		// Scratch pad variables used by actions
		private List<UhtToken> CurrentTypeTokens = new List<UhtToken>();
		private UhtToken CurrentTerminatingToken = new UhtToken();
		private int CurrentTemplateDepth = 0;

		// Actions/Delegates
		UhtTokensUntilDelegate GatherTypeTokensDelegate;

		private static Dictionary<StringView, UhtLayoutMacroType> LayoutMacroTypes = new Dictionary<StringView, UhtLayoutMacroType>(new[]
		{
			UhtLayoutMacroType.Array.MacroNameAndValue(),
			UhtLayoutMacroType.ArrayEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.Bitfield.MacroNameAndValue(),
			UhtLayoutMacroType.BitfieldEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.Field.MacroNameAndValue(),
			UhtLayoutMacroType.FieldEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.FieldInitialized.MacroNameAndValue(),
		});

		public UhtPropertyParser(UhtParsingScope Scope, IUhtMessageSite MessageSite)
		{
			this.SpecifierContext = new UhtPropertySpecifierContext(Scope, MessageSite);
			this.GatherTypeTokensDelegate = GatherTypeTokens;
		}

		public void Reset(UhtParsingScope Scope, IUhtMessageSite MessageSite)
		{
			this.SpecifierContext.Scope = Scope;
			this.SpecifierContext.MessageSite = MessageSite;
		}

		public UhtPropertyParser Parse(EPropertyFlags DisallowPropertyFlags, UhtPropertyParseOptions Options, UhtParsePropertyDeclarationStyle DeclarationStyle, UhtPropertyCategory Category, UhtPropertyDelegate Delegate)
		{
			// Reset the context and create the property
			this.SpecifierContext.PropertySettings.Reset(this.SpecifierContext.Scope.ScopeType, 0, Category, DisallowPropertyFlags);
			this.SpecifierContext.MetaData = this.SpecifierContext.PropertySettings.MetaData;
			this.SpecifierContext.MetaNameIndex = UhtMetaData.INDEX_NONE;
			this.SpecifierContext.bSeenEditSpecifier = false;
			this.SpecifierContext.bSeenBlueprintWriteSpecifier = false;
			this.SpecifierContext.bSeenBlueprintReadOnlySpecifier = false;
			this.SpecifierContext.bSeenBlueprintGetterSpecifier = false;

			// Initialize the property

			this.Options = Options;
			this.DeclarationStyle = DeclarationStyle;

			this.CurrentTypeTokens = new List<UhtToken>();
			this.CurrentTerminatingToken = new UhtToken();
			this.CurrentTemplateDepth = 0;

			using (var TokenContext = new UhtMessageContext(this.TokenReader, this))
			{
				ParseInternal(Delegate);
			}
			return this;
		}

		#region IMessageExtraContext implementation
		public IEnumerable<object?>? MessageExtraContext
		{
			get
			{
				Stack<object?> ExtraContext = new Stack<object?>(1);
				ExtraContext.Push(this.SpecifierContext.PropertySettings.PropertyCategory.GetHintText());
				return ExtraContext;
			}
		}
		#endregion

		/// <summary>
		/// Resolve the given property.  This method will resolve any immediate property during the parsing phase or 
		/// resolve any previously parsed property to the final version.
		/// </summary>
		/// <param name="ResolvePhase">Used to detect if the property is being parsed or resolved</param>
		/// <param name="Data">Character buffer for header file</param>
		/// <param name="PropertySettings">The property settings.</param>
		/// <param name="TypeTokens">The tokens that represent the type</param>
		/// <returns></returns>
		public static UhtProperty? ResolveProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, ReadOnlyMemory<char> Data, ReadOnlyMemory<UhtToken> TypeTokens)
		{
			UhtPropertyTypeTable PropertyTypeTable = UhtPropertyTypeTable.Instance;
			IUhtTokenReader ReplayReader = UhtTokenReplayReader.GetThreadInstance(PropertySettings.Outer, Data, TypeTokens, UhtTokenType.EndOfType);

			// Loop through the tokens until we find a known property type or the start of a template argument list
			UhtPropertyType PropertyType = new UhtPropertyType();
			for (int Index = 0; Index < TypeTokens.Length; ++Index)
			{
				if (TypeTokens.Span[Index].IsSymbol())
				{
					ReadOnlySpan<char> Span = TypeTokens.Span[Index].Value.Span;
					if (Span.Length == 1 && (Span[0] == '<' || Span[0] == '>' || Span[0] == ','))
					{
						break;
					}
				}
				else
				{
					if (PropertyTypeTable.TryGet(TypeTokens.Span[Index].Value, out PropertyType))
					{
						if (ResolvePhase == UhtPropertyResolvePhase.Resolving || PropertyType.Options.HasAnyFlags(UhtPropertyTypeOptions.Immediate))
						{
							return ResolvePropertyType(ResolvePhase, PropertySettings, ReplayReader, PropertyType, TypeTokens.Span[Index], false);
						}
					}
				}
			}

			// Try the default processor.  We only use the default processor when trying to resolve something post parsing phase.
			if (ResolvePhase == UhtPropertyResolvePhase.Resolving)
			{
				return ResolvePropertyType(ResolvePhase, PropertySettings, ReplayReader, PropertyTypeTable.Default, new UhtToken(), false);
			}
			return null;
		}

		private static ThreadLocal<UhtPropertySettings> TlsPropertySettings = new ThreadLocal<UhtPropertySettings>(() => { return new UhtPropertySettings(); });

		/// <summary>
		/// Given a type with children, resolve any children that couldn't be resolved during the parsing phase.
		/// </summary>
		/// <param name="Type">The type with children</param>
		/// <param name="Options">Parsing options</param>
		public static void ResolveChildren(UhtType Type, UhtPropertyParseOptions Options)
		{
			UhtPropertyOptions PropertyOptions = UhtPropertyOptions.None;
			if (Options.HasAnyFlags(UhtPropertyParseOptions.NoAutoConst))
			{
				PropertyOptions |= UhtPropertyOptions.NoAutoConst;
			}
			bool bInSymbolTable = Type.EngineType.AddChildrenToSymbolTable();

			UhtPropertySettings? PropertySettings = TlsPropertySettings.Value;
			if (PropertySettings == null)
			{
				throw new UhtIceException("Unable to acquire threaded property settings");
			}

			for (int Index = 0; Index < Type.Children.Count; ++Index)
			{
				if (Type.Children[Index] is UhtPreResolveProperty Property)
				{
					PropertySettings.Reset(Property, PropertyOptions);
					UhtProperty? Resolved = UhtPropertyParser.ResolveProperty(UhtPropertyResolvePhase.Resolving, PropertySettings, Property.HeaderFile.Data.Memory, Property.TypeTokens);
					if (Resolved != null)
					{
						if (bInSymbolTable && Resolved != Property)
						{
							Type.Session.ReplaceTypeInSymbolTable(Property, Resolved);
						}
						Type.Children[Index] = Resolved;
					}
				}
			}
		}

		public static UhtProperty? ParseTemplateParam(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings ParentPropertySettings, StringView ParamName, IUhtTokenReader TokenReader)
		{
			UhtPropertyTypeTable PropertyTypeTable = UhtPropertyTypeTable.Instance;

			// Save the token reader state.  We need this to restore back to the start when invoking the resolve methods.
			using (var SaveState = new UhtTokenSaveState(TokenReader))
			{
				UhtPropertySettings PropertySettings = new UhtPropertySettings(ParentPropertySettings, ParamName.ToString(), TokenReader);

				UhtPropertyType PropertyType = new UhtPropertyType();
				while (!TokenReader.bIsEOF)
				{
					UhtToken Token = TokenReader.GetToken();

					if (Token.IsSymbol('<') || Token.IsSymbol('>') || Token.IsSymbol(','))
					{
						break;
					}

					if (PropertyTypeTable.TryGet(Token.Value, out PropertyType))
					{
						if (ResolvePhase == UhtPropertyResolvePhase.Resolving || PropertyType.Options.HasAnyFlags(UhtPropertyTypeOptions.Immediate))
						{
							SaveState.RestoreState();
							return ResolvePropertyType(ResolvePhase, PropertySettings, TokenReader, PropertyType, Token, true);
						}
					}
				}

				// Try the default processor.  We only use the default processor when trying to resolve something post parsing phase.
				if (ResolvePhase == UhtPropertyResolvePhase.Resolving)
				{
					SaveState.RestoreState();
					return ResolvePropertyType(ResolvePhase, PropertySettings, TokenReader, PropertyTypeTable.Default, new UhtToken(), true);
				}
				return null;
			}
		}

		private static UhtProperty? ResolvePropertyType(UhtPropertyResolvePhase ResolvePhase,
			UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtPropertyType PropertyType, UhtToken MatchedToken, bool bIsTemplate)
		{
			UhtProperty? Out = PropertyType.Delegate(ResolvePhase, PropertySettings, TokenReader, MatchedToken);
			if (Out == null)
			{
				return null;
			}

			// If this is a simple type, skip the type
			if (PropertyType.Options.HasAnyFlags(UhtPropertyTypeOptions.Simple))
			{
				if (!TokenReader.SkipExpectedType(MatchedToken.Value, PropertySettings.PropertyCategory == UhtPropertyCategory.Member))
				{
					return null;
				}
			}

			// Handle any trailing const
			if (Out.PropertyCategory == UhtPropertyCategory.Member)
			{
				//@TODO: UCREMOVAL: 'const' member variables that will get written post-construction by defaultproperties
				UhtClass? OuterClass = Out.Outer as UhtClass;
				if (OuterClass != null && OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Const))
				{
					// Eat a 'not quite truthful' const after the type; autogenerated for member variables of const classes.
					if (TokenReader.TryOptional("const"))
					{
						Out.MetaData.Add(UhtNames.NativeConst, "");
					}
				}
			}
			else
			{
				if (TokenReader.TryOptional("const"))
				{
					Out.MetaData.Add(UhtNames.NativeConst, "");
					Out.PropertyFlags |= EPropertyFlags.ConstParm;
				}
			}

			// Check for unexpected '*'
			if (TokenReader.TryOptional('*'))
			{
				TokenReader.LogError($"Inappropriate '*' on variable of type '{Out.GetUserFacingDecl()}', cannot have an exposed pointer to this type.");
			}

			// Arrays are passed by reference but are only implicitly so; setting it explicitly could cause a problem with replicated functions
			if (TokenReader.TryOptional('&'))
			{
				switch (Out.PropertyCategory)
				{
					case UhtPropertyCategory.RegularParameter:
					case UhtPropertyCategory.Return:
						Out.PropertyFlags |= EPropertyFlags.OutParm;

						//@TODO: UCREMOVAL: How to determine if we have a ref param?
						if (Out.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
						{
							Out.PropertyFlags |= EPropertyFlags.ReferenceParm;
						}
						break;

					case UhtPropertyCategory.ReplicatedParameter:
						Out.PropertyFlags |= EPropertyFlags.ReferenceParm;
						break;

					default:
						break;
				}

				if (Out.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
				{
					Out.RefQualifier = UhtPropertyRefQualifier.ConstRef;
				}
				else
				{
					Out.RefQualifier = UhtPropertyRefQualifier.NonConstRef;
				}
			}

			if (!bIsTemplate)
			{
				if (!TokenReader.bIsEOF)
				{
					throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "end of type declaration");
				}
			}
			else
			{
				ref UhtToken Token = ref TokenReader.PeekToken();
				if (!Token.IsSymbol(',') && !Token.IsSymbol('>'))
				{
					throw new UhtTokenException(TokenReader, TokenReader.PeekToken(), "end of type declaration");
				}
			}
			return Out;
		}

		private void ParseInternal(UhtPropertyDelegate Delegate)
		{
			this.SpecifierContext.PropertySettings.LineNumber = this.TokenReader.InputLine;

			bool bIsParamList = this.SpecifierContext.PropertySettings.PropertyCategory != UhtPropertyCategory.Member && this.TokenReader.TryOptional("UPARAM");

			UhtSpecifierParser Specifiers = this.TopScope.HeaderParser.GetSpecifierParser(this.SpecifierContext, "Variable", 
				bIsParamList ? UhtPropertyArgumentSpecifiers.SpecifierTable : UhtPropertyMemberSpecifiers.SpecifierTable);
			if (DeclarationStyle == UhtParsePropertyDeclarationStyle.UPROPERTY || bIsParamList)
			{
				Specifiers.ParseSpecifiers();
			}

			if (this.SpecifierContext.PropertySettings.PropertyCategory != UhtPropertyCategory.Member)
			{
				// const before the variable type support (only for params)
				if (this.TokenReader.TryOptional("const"))
				{
					this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.ConstParm;
					this.SpecifierContext.PropertySettings.MetaData.Add(UhtNames.NativeConst, "");
				}
			}

			UhtCompilerDirective CompilerDirective = this.TopScope.HeaderParser.GetCurrentCompositeCompilerDirective();
			if (CompilerDirective.HasAnyFlags(UhtCompilerDirective.WithEditorOnlyData))
			{
				this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.EditorOnly;
			}
			else if (CompilerDirective.HasAnyFlags(UhtCompilerDirective.WithEditor))
			{
				// Checking for this error is a bit tricky given legacy code.  
				// 1) If already wrapped in WITH_EDITORONLY_DATA (see above), then we ignore the error via the else 
				// 2) Ignore any module that is an editor module
				UhtPackage Package = this.TopScope.HeaderParser.HeaderFile.Package;
				UHTManifest.Module Module = Package.Module;
				bool bIsEditorModule =
					Module.ModuleType == UHTModuleType.EngineEditor ||
					Module.ModuleType == UHTModuleType.GameEditor ||
					Module.ModuleType == UHTModuleType.EngineUncooked ||
					Module.ModuleType == UHTModuleType.GameUncooked;
				if (this.SpecifierContext.PropertySettings.PropertyCategory == UhtPropertyCategory.Member && !bIsEditorModule)
				{
					this.TokenReader.LogError("UProperties should not be wrapped by WITH_EDITOR, use WITH_EDITORONLY_DATA instead.");
				}
			}

			// Store the start and end positions of the parsed type
			int StartPos = this.TokenReader.InputPos;

			// Process the specifiers
			if (DeclarationStyle == UhtParsePropertyDeclarationStyle.UPROPERTY || bIsParamList)
			{
				Specifiers.ParseDeferred();
			}

			// If we saw a BlueprintGetter but did not see BlueprintSetter or 
			// or BlueprintReadWrite then treat as BlueprintReadOnly
			if (this.SpecifierContext.bSeenBlueprintGetterSpecifier && !this.SpecifierContext.bSeenBlueprintWriteSpecifier)
			{
				this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintReadOnly;
			}

			if (this.SpecifierContext.PropertySettings.MetaData.ContainsKey(UhtNames.ExposeOnSpawn))
			{
				this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.ExposeOnSpawn;
			}

			UhtAccessSpecifier AccessSpecifier = TopScope.AccessSpecifier;
			if (AccessSpecifier == UhtAccessSpecifier.Public || this.SpecifierContext.PropertySettings.PropertyCategory != UhtPropertyCategory.Member)
			{
				this.SpecifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.Protected;
				this.SpecifierContext.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Public;
				this.SpecifierContext.PropertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Private | UhtPropertyExportFlags.Protected);

				this.SpecifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
				this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierPublic;
			}
			else if (AccessSpecifier == UhtAccessSpecifier.Protected)
			{
				this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.Protected;
				this.SpecifierContext.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Protected;
				this.SpecifierContext.PropertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Public | UhtPropertyExportFlags.Private);

				this.SpecifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
				this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierProtected;
			}
			else if (AccessSpecifier == UhtAccessSpecifier.Private)
			{
				this.SpecifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.Protected;
				this.SpecifierContext.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Private;
				this.SpecifierContext.PropertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Public | UhtPropertyExportFlags.Protected);

				this.SpecifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
				this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierPrivate;
			}
			else
			{
				throw new UhtIceException("Unknown access level");
			}

			// Swallow inline keywords
			if (this.SpecifierContext.PropertySettings.PropertyCategory == UhtPropertyCategory.Return)
			{
				this.TokenReader
					.Optional("inline")
					.Optional("FORCENOINLINE")
					.OptionalStartsWith("FORCEINLINE");
			}

			// Handle MemoryLayout.h macros
			bool bHasWrapperBrackets = false;
			UhtLayoutMacroType LayoutMacroType = UhtLayoutMacroType.None;
			if (this.Options.HasAnyFlags(UhtPropertyParseOptions.ParseLayoutMacro))
			{
				ref UhtToken LayoutToken = ref this.TokenReader.PeekToken();
				if (LayoutToken.IsIdentifier())
				{
					if (LayoutMacroTypes.TryGetValue(LayoutToken.Value, out LayoutMacroType))
					{
						this.TokenReader.ConsumeToken();
						this.TokenReader.Require('(');
						bHasWrapperBrackets = this.TokenReader.TryOptional('(');
						if (LayoutMacroType.IsEditorOnly())
						{
							this.SpecifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.EditorOnly;
						}
					}
				}

				// This exists as a compatibility "shim" with UHT4/5.0.  If the fetched token wasn't an identifier,
				// it wasn't returned to the tokenizer.  So, just consume the token here.  In theory, this should be
				// removed once we have a good deprecated system.
				//@TODO - deprecate
				else // if (LayoutToken.IsSymbol(';'))
				{
					this.TokenReader.ConsumeToken();
				}
			}

			//@TODO: Should flag as settable from a const context, but this is at least good enough to allow use for C++ land
			this.TokenReader.Optional("mutable");

			// Gather the type tokens and possibly the property name.
			this.TokenReader.While(GatherTypeTokensDelegate);

			// Verify we at least have one type
			if (this.CurrentTypeTokens.Count < 1)
			{
				throw new UhtException(this.TokenReader, $"{this.SpecifierContext.PropertySettings.PropertyCategory.GetHintText()}: Missing variable type or name");
			}

			// Consume the wrapper brackets.  This is just an extra set
			if (bHasWrapperBrackets)
			{
				this.TokenReader.Require(')');
			}

			// Check for any disallowed flags
			if (this.SpecifierContext.PropertySettings.PropertyFlags.HasAnyFlags(this.SpecifierContext.PropertySettings.DisallowPropertyFlags))
			{
				this.TokenReader.LogError("Specified type modifiers not allowed here");
			}

			if (this.Options.HasAnyFlags(UhtPropertyParseOptions.AddModuleRelativePath))
			{
				UhtParsingScope.AddModuleRelativePathToMetaData(this.SpecifierContext.PropertySettings.MetaData, this.SpecifierContext.Scope.ScopeType.HeaderFile);
			}

			// Fetch the name of the property, bitfield and array size
			if (LayoutMacroType != UhtLayoutMacroType.None)
			{
				this.TokenReader.Require(',');
				UhtToken NameToken = this.TokenReader.GetIdentifier();
				if (LayoutMacroType.IsArray())
				{
					this.TokenReader.Require(',');
					RequireArray(this.SpecifierContext.PropertySettings, ref NameToken, ')');
				}
				else if (LayoutMacroType.IsBitfield())
				{
					this.TokenReader.Require(',');
					RequireBitfield(this.SpecifierContext.PropertySettings, ref NameToken);
				}
				else if (LayoutMacroType.HasInitializer())
				{
					this.TokenReader.SkipBrackets('(', ')', 1);
				}
				this.TokenReader.Require(')');

				Finalize(ref NameToken, new ReadOnlyMemory<UhtToken>(this.CurrentTypeTokens.ToArray()), LayoutMacroType, Delegate);
			}
			else if (this.Options.HasAnyFlags(UhtPropertyParseOptions.List))
			{
				// Extract the property name from the types
				if (this.CurrentTypeTokens.Count < 2 || !this.CurrentTypeTokens[this.CurrentTypeTokens.Count - 1].IsIdentifier())
				{
					throw new UhtException(this.TokenReader, $"{this.SpecifierContext.PropertySettings.PropertyCategory.GetHintText()}: Expected name");
				}
				UhtToken NameToken = this.CurrentTypeTokens[this.CurrentTypeTokens.Count - 1];
				this.CurrentTypeTokens.RemoveAt(this.CurrentTypeTokens.Count - 1);
				CheckForOptionalParts(this.SpecifierContext.PropertySettings, ref NameToken);

				ReadOnlyMemory<UhtToken> TypeTokens = new ReadOnlyMemory<UhtToken>(this.CurrentTypeTokens.ToArray());

				while (true)
				{
					UhtProperty Property = Finalize(ref NameToken, TypeTokens, LayoutMacroType, Delegate);

					// If we have reached the end
					if (!this.TokenReader.TryOptional(','))
					{
						break;
					}

					// While we could continue parsing, the old UHT would flag this as an error.
					throw new UhtException(TopScope.TokenReader, $"Comma delimited properties cannot be converted");

					// we'll need any metadata tags we parsed later on when we call ConvertEOLCommentToTooltip() so the tags aren't clobbered
					//this.SpecifierContext.PropertySettings.MetaData = Property.MetaData.Clone();
					//this.SpecifierContext.PropertySettings.ResetTrailingSettings();

					// Get the next property name
					//NameToken = this.TokenReader.GetIdentifier();
					//CheckForOptionalParts(this.SpecifierContext.PropertySettings, ref NameToken);
				}
			}
			else if (this.Options.HasAnyFlags(UhtPropertyParseOptions.CommaSeparatedName))
			{
				this.TokenReader.Require(',');
				UhtToken NameToken = this.TokenReader.GetIdentifier();
				CheckForOptionalParts(this.SpecifierContext.PropertySettings, ref NameToken);
				Finalize(ref NameToken, new ReadOnlyMemory<UhtToken>(this.CurrentTypeTokens.ToArray()), LayoutMacroType, Delegate);
			}
			else if (this.Options.HasAnyFlags(UhtPropertyParseOptions.FunctionNameIncluded))
			{
				UhtToken NameToken = this.CurrentTypeTokens[this.CurrentTypeTokens.Count - 1];
				NameToken.Value = new StringView("Function");
				if (CheckForOptionalParts(this.SpecifierContext.PropertySettings, ref NameToken))
				{
					NameToken = this.TokenReader.GetIdentifier("function name");
				}
				else
				{
					if (this.CurrentTypeTokens.Count < 2 || !this.CurrentTypeTokens[this.CurrentTypeTokens.Count - 1].IsIdentifier())
					{
						throw new UhtException(this.TokenReader, $"{this.SpecifierContext.PropertySettings.PropertyCategory.GetHintText()}: Expected name");
					}
					NameToken = this.CurrentTypeTokens[this.CurrentTypeTokens.Count - 1];
					this.CurrentTypeTokens.RemoveAt(this.CurrentTypeTokens.Count - 1);
				}
				Finalize(ref NameToken, new ReadOnlyMemory<UhtToken>(this.CurrentTypeTokens.ToArray()), LayoutMacroType, Delegate);
			}
			else if (this.Options.HasAnyFlags(UhtPropertyParseOptions.NameIncluded))
			{
				if (this.CurrentTypeTokens.Count < 2 || !this.CurrentTypeTokens[this.CurrentTypeTokens.Count - 1].IsIdentifier())
				{
					throw new UhtException(this.TokenReader, $"{this.SpecifierContext.PropertySettings.PropertyCategory.GetHintText()}: Expected name");
				}
				UhtToken NameToken = this.CurrentTypeTokens[this.CurrentTypeTokens.Count - 1];
				this.CurrentTypeTokens.RemoveAt(this.CurrentTypeTokens.Count - 1);
				CheckForOptionalParts(this.SpecifierContext.PropertySettings, ref NameToken);
				Finalize(ref NameToken, new ReadOnlyMemory<UhtToken>(this.CurrentTypeTokens.ToArray()), LayoutMacroType, Delegate);
			}
			else
			{
				UhtToken NameToken = new UhtToken();
				CheckForOptionalParts(this.SpecifierContext.PropertySettings, ref NameToken);
				Finalize(ref NameToken, new ReadOnlyMemory<UhtToken>(this.CurrentTypeTokens.ToArray()), LayoutMacroType, Delegate);
			}
		}

		/// <summary>
		/// Finish creating the property
		/// </summary>
		/// <param name="NameToken">The name of the property</param>
		/// <param name="TypeTokens">Series of tokens that represent the type</param>
		/// <param name="LayoutMacroType">Optional layout macro type being parsed</param>
		/// <param name="Delegate">Delegate to invoke when processing has been completed</param>
		/// <returns>The newly created property.  During the parsing phase, this will often be a temporary property if the type references engine types.</returns>
		private UhtProperty Finalize(ref UhtToken NameToken, ReadOnlyMemory<UhtToken> TypeTokens, UhtLayoutMacroType LayoutMacroType, UhtPropertyDelegate Delegate)
		{
			this.SpecifierContext.PropertySettings.SourceName = this.SpecifierContext.PropertySettings.PropertyCategory == UhtPropertyCategory.Return ? "ReturnValue" : NameToken.Value.ToString();

			// Try to resolve the property using any immediate mode property types
			UhtProperty? NewProperty = ResolveProperty(UhtPropertyResolvePhase.Parsing, this.SpecifierContext.PropertySettings, this.SpecifierContext.PropertySettings.Outer.HeaderFile.Data.Memory, TypeTokens);
			if (NewProperty == null)
			{
				NewProperty = new UhtPreResolveProperty(this.SpecifierContext.PropertySettings, TypeTokens);
			}

			// Force the category in non-engine projects
			if (NewProperty.PropertyCategory == UhtPropertyCategory.Member)
			{
				if (!NewProperty.Package.bIsPartOfEngine && 
					NewProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.BlueprintVisible) &&
					!NewProperty.MetaData.ContainsKey(UhtNames.Category))
				{
					NewProperty.MetaData.Add(UhtNames.Category, NewProperty.Outer!.EngineName);
				}
			}

			// Check to see if the variable is deprecated, and if so set the flag
			{
				int DeprecatedIndex = NewProperty.SourceName.IndexOf("_DEPRECATED");
				int NativizedPropertyPostfixIndex = NewProperty.SourceName.IndexOf("__pf"); //@TODO: check OverrideNativeName in Meta Data, to be sure it's not a random occurrence of the "__pf" string.
				bool bIgnoreDeprecatedWord = (NativizedPropertyPostfixIndex != -1) && (NativizedPropertyPostfixIndex > DeprecatedIndex);
				if ((DeprecatedIndex != -1) && !bIgnoreDeprecatedWord)
				{
					if (DeprecatedIndex != NewProperty.SourceName.Length - 11)
					{
						this.TokenReader.LogError("Deprecated variables must end with _DEPRECATED");
					}

					// We allow deprecated properties in blueprints that have getters and setters assigned as they may be part of a backwards compatibility path
					bool bBlueprintVisible = NewProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintVisible);
					bool bWarnOnGetter = bBlueprintVisible && !NewProperty.MetaData.ContainsKey(UhtNames.BlueprintGetter);
					bool bWarnOnSetter = bBlueprintVisible && !NewProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly) && !NewProperty.MetaData.ContainsKey(UhtNames.BlueprintSetter);

					if (bWarnOnGetter)
					{
						this.TokenReader.LogWarning($"{NewProperty.PropertyCategory.GetHintText()}: Deprecated property '{NewProperty.SourceName}' should not be marked as blueprint visible without having a BlueprintGetter");
					}

					if (bWarnOnSetter)
					{
						this.TokenReader.LogWarning($"{NewProperty.PropertyCategory.GetHintText()}: Deprecated property '{NewProperty.SourceName}' should not be marked as blueprint writable without having a BlueprintSetter");
					}

					// Warn if a deprecated property is visible
					if (NewProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.EditConst) || // Property is marked as editable
						(!bBlueprintVisible && NewProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly) && 
						!NewProperty.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.ImpliedBlueprintPure))) // Is BPRO, but not via Implied Flags and not caught by Getter/Setter path above
					{
						this.TokenReader.LogWarning($"{NewProperty.PropertyCategory.GetHintText()}: Deprecated property '{NewProperty.SourceName}' should not be marked as visible or editable");
					}

					NewProperty.PropertyFlags |= EPropertyFlags.Deprecated;
					NewProperty.EngineName = NewProperty.SourceName.Substring(0, DeprecatedIndex);
				}
			}

			// Try gathering metadata for member fields
			if (NewProperty.PropertyCategory == UhtPropertyCategory.Member)
			{
				UhtSpecifierParser Specifiers = this.TopScope.HeaderParser.GetSpecifierParser(this.SpecifierContext, NewProperty.SourceName, UhtPropertyMemberSpecifiers.SpecifierTable);
				Specifiers.ParseFieldMetaData(NewProperty.SourceName);
				this.TopScope.AddFormattedCommentsAsTooltipMetaData(NewProperty);
			}

			Delegate(this.TopScope, NewProperty, ref NameToken, LayoutMacroType);

			// Void properties don't get added when they are the return value
			if (NewProperty.PropertyCategory != UhtPropertyCategory.Return || !this.Options.HasAnyFlags(UhtPropertyParseOptions.DontAddReturn))
			{
				this.TopScope.ScopeType.AddChild(NewProperty);
			}
			return NewProperty;
		}

		private bool CheckForOptionalParts(UhtPropertySettings PropertySettings, ref UhtToken NameToken)
		{
			bool bGotOptionalParts = false;

			if (this.TokenReader.TryOptional('['))
			{
				RequireArray(PropertySettings, ref NameToken, ']');
				this.TokenReader.Require(']');
				bGotOptionalParts = true;
			}

			if (this.TokenReader.TryOptional(':'))
			{
				RequireBitfield(PropertySettings, ref NameToken);
				bGotOptionalParts = true;
			}
			return bGotOptionalParts;
		}

		private void RequireBitfield(UhtPropertySettings PropertySettings, ref UhtToken NameToken)
		{
			int BitfieldSize;
			if (!this.TokenReader.TryOptionalConstInt(out BitfieldSize) || BitfieldSize != 1)
			{
				throw new UhtException(this.TokenReader, $"Bad or missing bit field size for '{NameToken.Value}', must be 1.");
			}
			PropertySettings.bIsBitfield = true;
		}

		private void RequireArray(UhtPropertySettings PropertySettings, ref UhtToken NameToken, char Terminator)
		{
			// Ignore how the actual array dimensions are actually defined - we'll calculate those with the compiler anyway.
			PropertySettings.ArrayDimensions = this.TokenReader.GetRawString(Terminator, UhtRawStringOptions.DontConsumeTerminator).ToString();
			if (PropertySettings.ArrayDimensions.Length == 0)
			{
				throw new UhtException(this.TokenReader, $"{PropertySettings.PropertyCategory.GetHintText()} {NameToken.Value}: Missing array dimensions or terminating '{Terminator}'");
			}
		}

		private bool GatherTypeTokens(ref UhtToken Token)
		{
			if (this.CurrentTemplateDepth == 0 && Token.IsSymbol() && (Token.IsValue(',') || Token.IsValue('(') || Token.IsValue(')') || Token.IsValue(';') || Token.IsValue('[') || Token.IsValue(':') || Token.IsValue('=') || Token.IsValue('{')))
			{
				this.CurrentTerminatingToken = Token;
				return false;
			}

			this.CurrentTypeTokens.Add(Token);
			if (Token.IsSymbol('<'))
			{
				++this.CurrentTemplateDepth;
			}
			else if (Token.IsSymbol('>'))
			{
				if (this.CurrentTemplateDepth == 0)
				{
					throw new UhtTokenException(TokenReader, Token, "',' or ')'");
				}
				--this.CurrentTemplateDepth;
			}
			return true;
		}
	}

	[UnrealHeaderTool]
	public static class PropertyKeywords
	{
#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Class)]
		[UhtKeyword(Extends = UhtTableNames.ScriptStruct)]
		private static UhtParseResult UPROPERTYKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			UhtPropertyParseOptions Options = UhtPropertyParseOptions.ParseLayoutMacro | UhtPropertyParseOptions.List | UhtPropertyParseOptions.AddModuleRelativePath;
			TopScope.HeaderParser.GetPropertyParser(TopScope).Parse(EPropertyFlags.ParmFlags, Options, UhtParsePropertyDeclarationStyle.UPROPERTY, UhtPropertyCategory.Member, PropertyDelegate);
			TopScope.TokenReader.Require(';');
			return UhtParseResult.Handled;
		}
#endregion

		private static UhtPropertyDelegate PropertyDelegate = PropertyParsed;

		private static void PropertyParsed(UhtParsingScope TopScope, UhtProperty Property, ref UhtToken NameToken, UhtLayoutMacroType LayoutMacroType)
		{
			IUhtTokenReader TokenReader = TopScope.TokenReader;
			UhtStruct Struct = (UhtStruct)TopScope.ScopeType;

			// Skip any initialization
			if (TokenReader.TryOptional('='))
			{
				TokenReader.SkipUntil(';');
			}
			else if (TokenReader.TryOptional('{'))
			{
				TokenReader.SkipBrackets('{', '}', 1);
			}
		}
	}

	[UnrealHeaderTool]
	public static class UhtDefaultPropertyParser
	{
		[UhtPropertyType(Options = UhtPropertyTypeOptions.Default)]


		public static UhtProperty? DefaultProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			int TypeStartPos = TokenReader.PeekToken().InputStartPos;

			if (TokenReader.TryOptional("const"))
			{
				PropertySettings.MetaData.Add(UhtNames.NativeConst, "");
			}

			UhtFindOptions FindOptions = UhtFindOptions.DelegateFunction | UhtFindOptions.Enum | UhtFindOptions.Class | UhtFindOptions.ScriptStruct;
			if (TokenReader.TryOptional("enum"))
			{
				if (PropertySettings.PropertyCategory == UhtPropertyCategory.Member)
				{
					TokenReader.LogError($"Cannot declare enum at variable declaration");
				}
				FindOptions = UhtFindOptions.Enum;
			}
			else if (TokenReader.TryOptional("class"))
			{
				FindOptions = UhtFindOptions.Class;
			}
			else if (TokenReader.TryOptional("struct"))
			{
				FindOptions = UhtFindOptions.ScriptStruct;
			}

			UhtTokenList Identifiers = TokenReader.GetCppIdentifier();
			UhtType? Type = PropertySettings.Outer.FindType(UhtFindOptions.SourceName | FindOptions, Identifiers, TokenReader);
			if (Type == null)
			{
				return null;
			}
			UhtTokenListCache.Return(Identifiers);

			if (Type is UhtEnum Enum)
			{
				if (PropertySettings.PropertyCategory == UhtPropertyCategory.Member)
				{
					if (Enum.CppForm != UhtEnumCppForm.EnumClass)
					{
						TokenReader.LogError("You cannot use the raw enum name as a type for member variables, instead use TEnumAsByte or a C++11 enum class with an explicit underlying type.");
					}
				}

				return new UhtEnumProperty(PropertySettings, Enum);
			}
			else if (Type is UhtScriptStruct ScriptStruct)
			{
				return new UhtStructProperty(PropertySettings, ScriptStruct);
			}
			else if (Type is UhtFunction Function)
			{
				if (!Function.FunctionFlags.HasAnyFlags(EFunctionFlags.MulticastDelegate))
				{
					return new UhtDelegateProperty(PropertySettings, Function);
				}
				else if (Function.FunctionType == UhtFunctionType.SparseDelegate)
				{
					return new UhtMulticastSparseDelegateProperty(PropertySettings, Function);
				}
				else
				{
					return new UhtMulticastInlineDelegateProperty(PropertySettings, Function);
				}
			}
			else if (Type is UhtClass Class)
			{
				// Const after variable type but before pointer symbol
				if (TokenReader.TryOptional("const"))
				{
					PropertySettings.MetaData.Add(UhtNames.NativeConst, "");
				}
				TokenReader.Require('*');

				// Optionally emit messages about native pointer members and swallow trailing 'const' after pointer properties
				UhtObjectPropertyBase.ConditionalLogPointerUsage(PropertySettings, UhtConfig.Instance.EngineNativePointerMemberBehavior,
					UhtConfig.Instance.NonEngineNativePointerMemberBehavior, "Native pointer", TokenReader, TypeStartPos, "TObjectPtr");

				if (PropertySettings.PropertyCategory == UhtPropertyCategory.Member)
				{
					TokenReader.TryOptional("const");
				}

				PropertySettings.PointerType = UhtPointerType.Native;

				if (Class.ClassFlags.HasAnyFlags(EClassFlags.Interface))
				{
					return new UhtInterfaceProperty(PropertySettings, Class);
				}
				else if (Class.IsChildOf(Class.Session.UClass))
				{
					// UObject here specifies that there is no limiter
					return new UhtClassProperty(PropertySettings, Class, Class.Session.UObject);
				}
				else
				{
					return new UhtObjectProperty(PropertySettings, Class);
				}
			}
			else
			{
				throw new UhtIceException("Unexpected type found");
			}
		}
	}
}
