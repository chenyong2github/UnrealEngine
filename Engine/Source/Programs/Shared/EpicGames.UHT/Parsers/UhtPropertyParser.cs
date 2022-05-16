// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Threading;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	/// <summary>
	/// Options that customize the parsing of properties.
	/// </summary>
	[Flags]
	public enum UhtPropertyParseOptions
	{

		/// <summary>
		/// No options
		/// </summary>
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
		/// <param name="inFlags">Current flags</param>
		/// <param name="testFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtPropertyParseOptions inFlags, UhtPropertyParseOptions testFlags)
		{
			return (inFlags & testFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="inFlags">Current flags</param>
		/// <param name="testFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtPropertyParseOptions inFlags, UhtPropertyParseOptions testFlags)
		{
			return (inFlags & testFlags) == testFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="inFlags">Current flags</param>
		/// <param name="testFlags">Flags to test for</param>
		/// <param name="matchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtPropertyParseOptions inFlags, UhtPropertyParseOptions testFlags, UhtPropertyParseOptions matchFlags)
		{
			return (inFlags & testFlags) == matchFlags;
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

	/// <summary>
	/// Layout macro type
	/// </summary>
	public enum UhtLayoutMacroType
	{

		/// <summary>
		/// None found
		/// </summary>
		None,

		/// <summary>
		/// Array
		/// </summary>
		Array,

		/// <summary>
		/// Editor only array
		/// </summary>
		ArrayEditorOnly,

		/// <summary>
		/// Bit field
		/// </summary>
		Bitfield,

		/// <summary>
		/// Editor only bit field
		/// </summary>
		BitfieldEditorOnly,

		/// <summary>
		/// Field
		/// </summary>
		Field,

		/// <summary>
		/// Editor only field
		/// </summary>
		FieldEditorOnly,

		/// <summary>
		/// Field with initializer
		/// </summary>
		FieldInitialized,
	}

	/// <summary>
	/// Extensions for working with the layout macro type
	/// </summary>
	public static class UhtLayoutMacroTypeExtensions
	{

		/// <summary>
		/// Return true if the type is editor only
		/// </summary>
		/// <param name="layoutMacroType">Layout macro type</param>
		/// <returns>True if editor only</returns>
		public static bool IsEditorOnly(this UhtLayoutMacroType layoutMacroType)
		{
			switch (layoutMacroType)
			{
				case UhtLayoutMacroType.ArrayEditorOnly:
				case UhtLayoutMacroType.BitfieldEditorOnly:
				case UhtLayoutMacroType.FieldEditorOnly:
					return true;

				default:
					return false;
			}
		}

		/// <summary>
		/// Return true if the type is a bit field
		/// </summary>
		/// <param name="layoutMacroType">Layout macro type</param>
		/// <returns>True if bit field</returns>
		public static bool IsBitfield(this UhtLayoutMacroType layoutMacroType)
		{
			switch (layoutMacroType)
			{
				case UhtLayoutMacroType.Bitfield:
				case UhtLayoutMacroType.BitfieldEditorOnly:
					return true;

				default:
					return false;
			}
		}

		/// <summary>
		/// Return true if the type is an array
		/// </summary>
		/// <param name="layoutMacroType">Layout macro type</param>
		/// <returns>True if array</returns>
		public static bool IsArray(this UhtLayoutMacroType layoutMacroType)
		{
			switch (layoutMacroType)
			{
				case UhtLayoutMacroType.Array:
				case UhtLayoutMacroType.ArrayEditorOnly:
					return true;

				default:
					return false;
			}
		}

		/// <summary>
		/// Return true if the type has an initializer
		/// </summary>
		/// <param name="layoutMacroType">Layout macro type</param>
		/// <returns>True if it has an initializer</returns>
		public static bool HasInitializer(this UhtLayoutMacroType layoutMacroType)
		{
			switch (layoutMacroType)
			{
				case UhtLayoutMacroType.FieldInitialized:
					return true;

				default:
					return false;
			}
		}

		/// <summary>
		/// Return the layout macro name
		/// </summary>
		/// <param name="layoutMacroType">Type in question</param>
		/// <returns>Macro name</returns>
		/// <exception cref="UhtIceException">Thrown if the macro type is none or invalid</exception>
		public static StringView MacroName(this UhtLayoutMacroType layoutMacroType)
		{
			switch (layoutMacroType)
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

		/// <summary>
		/// Return the macro name and value
		/// </summary>
		/// <param name="layoutMacroType">Macro name</param>
		/// <returns>Name and type</returns>
		public static KeyValuePair<StringView, UhtLayoutMacroType> MacroNameAndValue(this UhtLayoutMacroType layoutMacroType)
		{
			return new KeyValuePair<StringView, UhtLayoutMacroType>(layoutMacroType.MacroName(), layoutMacroType);
		}
	}

	/// <summary>
	/// Delegate invoked to handle a parsed property 
	/// </summary>
	/// <param name="topScope">Scope being parsed</param>
	/// <param name="property">Property just parsed</param>
	/// <param name="nameToken">Name of the property</param>
	/// <param name="layoutMacroType">Layout macro type</param>
	public delegate void UhtPropertyDelegate(UhtParsingScope topScope, UhtProperty property, ref UhtToken nameToken, UhtLayoutMacroType layoutMacroType);

	/// <summary>
	/// Context for property specifier parsing
	/// </summary>
	public class UhtPropertySpecifierContext : UhtSpecifierContext
	{
		/// <summary>
		/// The property settings being parsed
		/// </summary>
		public UhtPropertySettings PropertySettings { get; set; }

		/// <summary>
		/// If true, editor specifier seen
		/// </summary>
		public bool SeenEditSpecifier { get; set; } = false;

		/// <summary>
		/// If true, blueprint write specifier seen
		/// </summary>
		public bool SeenBlueprintWriteSpecifier { get; set; } = false;

		/// <summary>
		/// If true, blueprint readonly specifier seen
		/// </summary>
		public bool SeenBlueprintReadOnlySpecifier { get; set; } = false;

		/// <summary>
		/// If true, blueprint getter specifier seen
		/// </summary>
		public bool SeenBlueprintGetterSpecifier { get; set; } = false;

		/// <summary>
		/// Construct a new property specifier context
		/// </summary>
		/// <param name="scope">Scope being parsed</param>
		/// <param name="messageSite">Message site</param>
		public UhtPropertySpecifierContext(UhtParsingScope scope, IUhtMessageSite messageSite) : base(scope, messageSite, UhtMetaData.Empty)
		{
			this.PropertySettings = new UhtPropertySettings();
			this.PropertySettings.Reset(scope.ScopeType, 1, UhtPropertyCategory.Member, 0);
		}
	}

	/// <summary>
	/// A parsed property is a property that was parsed but couldn't yet be resolved.  It retains the list of tokens needed
	/// to resolve the type of the property.  It will be replaced with the resolved property type during property resolution.
	/// </summary>
	public class UhtPreResolveProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName => "UHTParsedProperty";

		/// <inheritdoc/>
		protected override string CppTypeText => "invalid";

		/// <inheritdoc/>
		protected override string PGetMacroText => "invalid";

		/// <summary>
		/// Collection of type tokens 
		/// </summary>
		public ReadOnlyMemory<UhtToken> TypeTokens { get; set; }

		/// <summary>
		/// Property settings being parsed
		/// </summary>
		public UhtPropertySettings PropertySettings { get; set; }

		/// <summary>
		/// Construct a new property to be resolved
		/// </summary>
		/// <param name="propertySettings">Property settings</param>
		/// <param name="typeTokens">Type tokens</param>
		public UhtPreResolveProperty(UhtPropertySettings propertySettings, ReadOnlyMemory<UhtToken> typeTokens) : base(propertySettings)
		{
			this.TypeTokens = typeTokens;
			this.PropertySettings = propertySettings;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty other)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder builder, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, string? offset, int tabs)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterGet(StringBuilder builder)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder builder, bool isInitializer)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader defaultValueReader, StringBuilder innerDefaultValue)
		{
			throw new NotImplementedException();
		}
	}

	/// <summary>
	/// Property parser
	/// </summary>
	public class UhtPropertyParser : IUhtMessageExtraContext
	{
		private readonly UhtPropertySpecifierContext _specifierContext;
		private UhtPropertyParseOptions _options;
		private UhtParsePropertyDeclarationStyle _declarationStyle;

		private IUhtTokenReader TokenReader => this._specifierContext.Scope.TokenReader;
		private UhtParsingScope TopScope => this._specifierContext.Scope;

		// Scratch pad variables used by actions
		private List<UhtToken> _currentTypeTokens = new();
		private int _currentTemplateDepth = 0;

		// Actions/Delegates
		private readonly UhtTokensUntilDelegate _gatherTypeTokensDelegate;

		private static readonly Dictionary<StringView, UhtLayoutMacroType> s_layoutMacroTypes = new(new[]
		{
			UhtLayoutMacroType.Array.MacroNameAndValue(),
			UhtLayoutMacroType.ArrayEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.Bitfield.MacroNameAndValue(),
			UhtLayoutMacroType.BitfieldEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.Field.MacroNameAndValue(),
			UhtLayoutMacroType.FieldEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.FieldInitialized.MacroNameAndValue(),
		});

		/// <summary>
		/// Construct a new property parser
		/// </summary>
		/// <param name="scope">Current scope being parsed</param>
		/// <param name="messageSite">Message site</param>
		public UhtPropertyParser(UhtParsingScope scope, IUhtMessageSite messageSite)
		{
			this._specifierContext = new UhtPropertySpecifierContext(scope, messageSite);
			this._gatherTypeTokensDelegate = GatherTypeTokens;
		}

		/// <summary>
		/// Reset the property parser for a new parse
		/// </summary>
		/// <param name="scope">Current scope being parsed</param>
		/// <param name="messageSite">Message site</param>
		public void Reset(UhtParsingScope scope, IUhtMessageSite messageSite)
		{
			this._specifierContext.Scope = scope;
			this._specifierContext.MessageSite = messageSite;
		}

		/// <summary>
		/// Parse the property
		/// </summary>
		/// <param name="disallowPropertyFlags">Flags to be disallowed</param>
		/// <param name="options">Parsing options</param>
		/// <param name="declarationStyle">Style of declaration</param>
		/// <param name="category">Property category</param>
		/// <param name="propertyDelegate">Delegate to be invoked after property has been parsed</param>
		/// <returns>The property parser</returns>
		public UhtPropertyParser Parse(EPropertyFlags disallowPropertyFlags, UhtPropertyParseOptions options, UhtParsePropertyDeclarationStyle declarationStyle, UhtPropertyCategory category, UhtPropertyDelegate propertyDelegate)
		{
			// Reset the context and create the property
			this._specifierContext.PropertySettings.Reset(this._specifierContext.Scope.ScopeType, 0, category, disallowPropertyFlags);
			this._specifierContext.MetaData = this._specifierContext.PropertySettings.MetaData;
			this._specifierContext.MetaNameIndex = UhtMetaData.IndexNone;
			this._specifierContext.SeenEditSpecifier = false;
			this._specifierContext.SeenBlueprintWriteSpecifier = false;
			this._specifierContext.SeenBlueprintReadOnlySpecifier = false;
			this._specifierContext.SeenBlueprintGetterSpecifier = false;

			// Initialize the property

			this._options = options;
			this._declarationStyle = declarationStyle;

			this._currentTypeTokens = new List<UhtToken>();
			this._currentTemplateDepth = 0;

			using UhtMessageContext tokenContext = new(this);
			ParseInternal(propertyDelegate);
			return this;
		}

		#region IMessageExtraContext implementation
		/// <inheritdoc/>
		public IEnumerable<object?>? MessageExtraContext
		{
			get
			{
				Stack<object?> extraContext = new(1);
				extraContext.Push(this._specifierContext.PropertySettings.PropertyCategory.GetHintText());
				return extraContext;
			}
		}
		#endregion

		/// <summary>
		/// Resolve the given property.  This method will resolve any immediate property during the parsing phase or 
		/// resolve any previously parsed property to the final version.
		/// </summary>
		/// <param name="resolvePhase">Used to detect if the property is being parsed or resolved</param>
		/// <param name="data">Character buffer for header file</param>
		/// <param name="propertySettings">The property settings.</param>
		/// <param name="typeTokens">The tokens that represent the type</param>
		/// <returns></returns>
		public static UhtProperty? ResolveProperty(UhtPropertyResolvePhase resolvePhase, UhtPropertySettings propertySettings, ReadOnlyMemory<char> data, ReadOnlyMemory<UhtToken> typeTokens)
		{
			UhtSession session = propertySettings.Outer.Session;
			IUhtTokenReader replayReader = UhtTokenReplayReader.GetThreadInstance(propertySettings.Outer, data, typeTokens, UhtTokenType.EndOfType);

			// Loop through the tokens until we find a known property type or the start of a template argument list
			for (int index = 0; index < typeTokens.Length; ++index)
			{
				if (typeTokens.Span[index].IsSymbol())
				{
					ReadOnlySpan<char> span = typeTokens.Span[index].Value.Span;
					if (span.Length == 1 && (span[0] == '<' || span[0] == '>' || span[0] == ','))
					{
						break;
					}
				}
				else
				{
					if (session.TryGetPropertyType(typeTokens.Span[index].Value, out UhtPropertyType propertyType))
					{
						if (resolvePhase == UhtPropertyResolvePhase.Resolving || propertyType.Options.HasAnyFlags(UhtPropertyTypeOptions.Immediate))
						{
							return ResolvePropertyType(resolvePhase, propertySettings, replayReader, propertyType, typeTokens.Span[index], false);
						}
					}
				}
			}

			// Try the default processor.  We only use the default processor when trying to resolve something post parsing phase.
			if (resolvePhase == UhtPropertyResolvePhase.Resolving)
			{
				return ResolvePropertyType(resolvePhase, propertySettings, replayReader, session.DefaultPropertyType, new UhtToken(), false);
			}
			return null;
		}

		private static readonly ThreadLocal<UhtPropertySettings> s_tlsPropertySettings = new(() => { return new UhtPropertySettings(); });

		/// <summary>
		/// Given a type with children, resolve any children that couldn't be resolved during the parsing phase.
		/// </summary>
		/// <param name="type">The type with children</param>
		/// <param name="options">Parsing options</param>
		public static void ResolveChildren(UhtType type, UhtPropertyParseOptions options)
		{
			UhtPropertyOptions propertyOptions = UhtPropertyOptions.None;
			if (options.HasAnyFlags(UhtPropertyParseOptions.NoAutoConst))
			{
				propertyOptions |= UhtPropertyOptions.NoAutoConst;
			}
			bool inSymbolTable = type.EngineType.AddChildrenToSymbolTable();

			UhtPropertySettings? propertySettings = s_tlsPropertySettings.Value;
			if (propertySettings == null)
			{
				throw new UhtIceException("Unable to acquire threaded property settings");
			}

			for (int index = 0; index < type.Children.Count; ++index)
			{
				if (type.Children[index] is UhtPreResolveProperty property)
				{
					propertySettings.Reset(property, propertyOptions);
					UhtProperty? resolved = UhtPropertyParser.ResolveProperty(UhtPropertyResolvePhase.Resolving, propertySettings, property.HeaderFile.Data.Memory, property.TypeTokens);
					if (resolved != null)
					{
						if (inSymbolTable && resolved != property)
						{
							type.Session.ReplaceTypeInSymbolTable(property, resolved);
						}
						type.Children[index] = resolved;
					}
				}
			}
		}

		/// <summary>
		/// Parse a template parameter
		/// </summary>
		/// <param name="resolvePhase">Resolution phase</param>
		/// <param name="parentPropertySettings">Parent property (container) settings</param>
		/// <param name="paramName">Name of the template parameter</param>
		/// <param name="tokenReader">Token type</param>
		/// <returns>Parsed property</returns>
		public static UhtProperty? ParseTemplateParam(UhtPropertyResolvePhase resolvePhase, UhtPropertySettings parentPropertySettings, StringView paramName, IUhtTokenReader tokenReader)
		{
			UhtSession session = parentPropertySettings.Outer.Session;

			// Save the token reader state.  We need this to restore back to the start when invoking the resolve methods.
			{
				using UhtTokenSaveState saveState = new(tokenReader);
				UhtPropertySettings propertySettings = new(parentPropertySettings, paramName.ToString(), tokenReader);

				UhtPropertyType propertyType = new();
				while (!tokenReader.IsEOF)
				{
					UhtToken token = tokenReader.GetToken();

					if (token.IsSymbol('<') || token.IsSymbol('>') || token.IsSymbol(','))
					{
						break;
					}

					if (session.TryGetPropertyType(token.Value, out propertyType))
					{
						if (resolvePhase == UhtPropertyResolvePhase.Resolving || propertyType.Options.HasAnyFlags(UhtPropertyTypeOptions.Immediate))
						{
							saveState.RestoreState();
							return ResolvePropertyType(resolvePhase, propertySettings, tokenReader, propertyType, token, true);
						}
					}
				}

				// Try the default processor.  We only use the default processor when trying to resolve something post parsing phase.
				if (resolvePhase == UhtPropertyResolvePhase.Resolving)
				{
					saveState.RestoreState();
					return ResolvePropertyType(resolvePhase, propertySettings, tokenReader, session.DefaultPropertyType, new UhtToken(), true);
				}
				return null;
			}
		}

		private static UhtProperty? ResolvePropertyType(UhtPropertyResolvePhase resolvePhase,
			UhtPropertySettings propertySettings, IUhtTokenReader tokenReader, UhtPropertyType propertyType, UhtToken matchedToken, bool isTemplate)
		{
			UhtProperty? outProperty = propertyType.Delegate(resolvePhase, propertySettings, tokenReader, matchedToken);
			if (outProperty == null)
			{
				return null;
			}

			// If this is a simple type, skip the type
			if (propertyType.Options.HasAnyFlags(UhtPropertyTypeOptions.Simple))
			{
				if (!tokenReader.SkipExpectedType(matchedToken.Value, propertySettings.PropertyCategory == UhtPropertyCategory.Member))
				{
					return null;
				}
			}

			// Handle any trailing const
			if (outProperty.PropertyCategory == UhtPropertyCategory.Member)
			{
				//@TODO: UCREMOVAL: 'const' member variables that will get written post-construction by defaultproperties
				UhtClass? outerClass = outProperty.Outer as UhtClass;
				if (outerClass != null && outerClass.ClassFlags.HasAnyFlags(EClassFlags.Const))
				{
					// Eat a 'not quite truthful' const after the type; autogenerated for member variables of const classes.
					if (tokenReader.TryOptional("const"))
					{
						outProperty.MetaData.Add(UhtNames.NativeConst, "");
					}
				}
			}
			else
			{
				if (tokenReader.TryOptional("const"))
				{
					outProperty.MetaData.Add(UhtNames.NativeConst, "");
					outProperty.PropertyFlags |= EPropertyFlags.ConstParm;
				}
			}

			// Check for unexpected '*'
			if (tokenReader.TryOptional('*'))
			{
				tokenReader.LogError($"Inappropriate '*' on variable of type '{outProperty.GetUserFacingDecl()}', cannot have an exposed pointer to this type.");
			}

			// Arrays are passed by reference but are only implicitly so; setting it explicitly could cause a problem with replicated functions
			if (tokenReader.TryOptional('&'))
			{
				switch (outProperty.PropertyCategory)
				{
					case UhtPropertyCategory.RegularParameter:
					case UhtPropertyCategory.Return:
						outProperty.PropertyFlags |= EPropertyFlags.OutParm;

						//@TODO: UCREMOVAL: How to determine if we have a ref param?
						if (outProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
						{
							outProperty.PropertyFlags |= EPropertyFlags.ReferenceParm;
						}
						break;

					case UhtPropertyCategory.ReplicatedParameter:
						outProperty.PropertyFlags |= EPropertyFlags.ReferenceParm;
						break;

					default:
						break;
				}

				if (outProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
				{
					outProperty.RefQualifier = UhtPropertyRefQualifier.ConstRef;
				}
				else
				{
					outProperty.RefQualifier = UhtPropertyRefQualifier.NonConstRef;
				}
			}

			if (!isTemplate)
			{
				if (!tokenReader.IsEOF)
				{
					throw new UhtTokenException(tokenReader, tokenReader.PeekToken(), "end of type declaration");
				}
			}
			else
			{
				ref UhtToken token = ref tokenReader.PeekToken();
				if (!token.IsSymbol(',') && !token.IsSymbol('>'))
				{
					throw new UhtTokenException(tokenReader, tokenReader.PeekToken(), "end of type declaration");
				}
			}
			return outProperty;
		}

		private void ParseInternal(UhtPropertyDelegate propertyDelegate)
		{
			this._specifierContext.PropertySettings.LineNumber = this.TokenReader.InputLine;

			bool isParamList = this._specifierContext.PropertySettings.PropertyCategory != UhtPropertyCategory.Member && this.TokenReader.TryOptional("UPARAM");

			UhtSpecifierParser specifiers = UhtSpecifierParser.GetThreadInstance(this._specifierContext, "Variable",
				isParamList ? this.TopScope.Session.GetSpecifierTable(UhtTableNames.PropertyArgument) : this.TopScope.Session.GetSpecifierTable(UhtTableNames.PropertyMember));
			if (_declarationStyle == UhtParsePropertyDeclarationStyle.UPROPERTY || isParamList)
			{
				specifiers.ParseSpecifiers();
			}

			if (this._specifierContext.PropertySettings.PropertyCategory != UhtPropertyCategory.Member)
			{
				// const before the variable type support (only for params)
				if (this.TokenReader.TryOptional("const"))
				{
					this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.ConstParm;
					this._specifierContext.PropertySettings.MetaData.Add(UhtNames.NativeConst, "");
				}
			}

			UhtCompilerDirective compilerDirective = this.TopScope.HeaderParser.GetCurrentCompositeCompilerDirective();
			if (compilerDirective.HasAnyFlags(UhtCompilerDirective.WithEditorOnlyData))
			{
				this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.EditorOnly;
			}
			else if (compilerDirective.HasAnyFlags(UhtCompilerDirective.WithEditor))
			{
				// Checking for this error is a bit tricky given legacy code.  
				// 1) If already wrapped in WITH_EDITORONLY_DATA (see above), then we ignore the error via the else 
				// 2) Ignore any module that is an editor module
				UhtPackage package = this.TopScope.HeaderParser.HeaderFile.Package;
				UHTManifest.Module module = package.Module;
				bool isEditorModule =
					module.ModuleType == UHTModuleType.EngineEditor ||
					module.ModuleType == UHTModuleType.GameEditor ||
					module.ModuleType == UHTModuleType.EngineUncooked ||
					module.ModuleType == UHTModuleType.GameUncooked;
				if (this._specifierContext.PropertySettings.PropertyCategory == UhtPropertyCategory.Member && !isEditorModule)
				{
					this.TokenReader.LogError("UProperties should not be wrapped by WITH_EDITOR, use WITH_EDITORONLY_DATA instead.");
				}
			}

			// Store the start and end positions of the parsed type
			int startPos = this.TokenReader.InputPos;

			// Process the specifiers
			if (_declarationStyle == UhtParsePropertyDeclarationStyle.UPROPERTY || isParamList)
			{
				specifiers.ParseDeferred();
			}

			// If we saw a BlueprintGetter but did not see BlueprintSetter or 
			// or BlueprintReadWrite then treat as BlueprintReadOnly
			if (this._specifierContext.SeenBlueprintGetterSpecifier && !this._specifierContext.SeenBlueprintWriteSpecifier)
			{
				this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintReadOnly;
			}

			if (this._specifierContext.PropertySettings.MetaData.ContainsKey(UhtNames.ExposeOnSpawn))
			{
				this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.ExposeOnSpawn;
			}

			UhtAccessSpecifier accessSpecifier = TopScope.AccessSpecifier;
			if (accessSpecifier == UhtAccessSpecifier.Public || this._specifierContext.PropertySettings.PropertyCategory != UhtPropertyCategory.Member)
			{
				this._specifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.Protected;
				this._specifierContext.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Public;
				this._specifierContext.PropertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Private | UhtPropertyExportFlags.Protected);

				this._specifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
				this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierPublic;
			}
			else if (accessSpecifier == UhtAccessSpecifier.Protected)
			{
				this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.Protected;
				this._specifierContext.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Protected;
				this._specifierContext.PropertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Public | UhtPropertyExportFlags.Private);

				this._specifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
				this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierProtected;
			}
			else if (accessSpecifier == UhtAccessSpecifier.Private)
			{
				this._specifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.Protected;
				this._specifierContext.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Private;
				this._specifierContext.PropertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Public | UhtPropertyExportFlags.Protected);

				this._specifierContext.PropertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
				this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierPrivate;
			}
			else
			{
				throw new UhtIceException("Unknown access level");
			}

			// Swallow inline keywords
			if (this._specifierContext.PropertySettings.PropertyCategory == UhtPropertyCategory.Return)
			{
				this.TokenReader
					.Optional("inline")
					.Optional("FORCENOINLINE")
					.OptionalStartsWith("FORCEINLINE");
			}

			// Handle MemoryLayout.h macros
			bool hasWrapperBrackets = false;
			UhtLayoutMacroType layoutMacroType = UhtLayoutMacroType.None;
			if (this._options.HasAnyFlags(UhtPropertyParseOptions.ParseLayoutMacro))
			{
				ref UhtToken layoutToken = ref this.TokenReader.PeekToken();
				if (layoutToken.IsIdentifier())
				{
					if (s_layoutMacroTypes.TryGetValue(layoutToken.Value, out layoutMacroType))
					{
						this.TokenReader.ConsumeToken();
						this.TokenReader.Require('(');
						hasWrapperBrackets = this.TokenReader.TryOptional('(');
						if (layoutMacroType.IsEditorOnly())
						{
							this._specifierContext.PropertySettings.PropertyFlags |= EPropertyFlags.EditorOnly;
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
			this.TokenReader.While(_gatherTypeTokensDelegate);

			// Verify we at least have one type
			if (this._currentTypeTokens.Count < 1)
			{
				throw new UhtException(this.TokenReader, $"{this._specifierContext.PropertySettings.PropertyCategory.GetHintText()}: Missing variable type or name");
			}

			// Consume the wrapper brackets.  This is just an extra set
			if (hasWrapperBrackets)
			{
				this.TokenReader.Require(')');
			}

			// Check for any disallowed flags
			if (this._specifierContext.PropertySettings.PropertyFlags.HasAnyFlags(this._specifierContext.PropertySettings.DisallowPropertyFlags))
			{
				this.TokenReader.LogError("Specified type modifiers not allowed here");
			}

			if (this._options.HasAnyFlags(UhtPropertyParseOptions.AddModuleRelativePath))
			{
				UhtParsingScope.AddModuleRelativePathToMetaData(this._specifierContext.PropertySettings.MetaData, this._specifierContext.Scope.ScopeType.HeaderFile);
			}

			// Fetch the name of the property, bitfield and array size
			if (layoutMacroType != UhtLayoutMacroType.None)
			{
				this.TokenReader.Require(',');
				UhtToken nameToken = this.TokenReader.GetIdentifier();
				if (layoutMacroType.IsArray())
				{
					this.TokenReader.Require(',');
					RequireArray(this._specifierContext.PropertySettings, ref nameToken, ')');
				}
				else if (layoutMacroType.IsBitfield())
				{
					this.TokenReader.Require(',');
					RequireBitfield(this._specifierContext.PropertySettings, ref nameToken);
				}
				else if (layoutMacroType.HasInitializer())
				{
					this.TokenReader.SkipBrackets('(', ')', 1);
				}
				this.TokenReader.Require(')');

				Finalize(ref nameToken, new ReadOnlyMemory<UhtToken>(this._currentTypeTokens.ToArray()), layoutMacroType, propertyDelegate);
			}
			else if (this._options.HasAnyFlags(UhtPropertyParseOptions.List))
			{
				// Extract the property name from the types
				if (this._currentTypeTokens.Count < 2 || !this._currentTypeTokens[^1].IsIdentifier())
				{
					throw new UhtException(this.TokenReader, $"{this._specifierContext.PropertySettings.PropertyCategory.GetHintText()}: Expected name");
				}
				UhtToken nameToken = this._currentTypeTokens[^1];
				this._currentTypeTokens.RemoveAt(this._currentTypeTokens.Count - 1);
				CheckForOptionalParts(this._specifierContext.PropertySettings, ref nameToken);

				ReadOnlyMemory<UhtToken> typeTokens = new(this._currentTypeTokens.ToArray());

				while (true)
				{
					UhtProperty _ = Finalize(ref nameToken, typeTokens, layoutMacroType, propertyDelegate);

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
			else if (this._options.HasAnyFlags(UhtPropertyParseOptions.CommaSeparatedName))
			{
				this.TokenReader.Require(',');
				UhtToken nameToken = this.TokenReader.GetIdentifier();
				CheckForOptionalParts(this._specifierContext.PropertySettings, ref nameToken);
				Finalize(ref nameToken, new ReadOnlyMemory<UhtToken>(this._currentTypeTokens.ToArray()), layoutMacroType, propertyDelegate);
			}
			else if (this._options.HasAnyFlags(UhtPropertyParseOptions.FunctionNameIncluded))
			{
				UhtToken nameToken = this._currentTypeTokens[^1];
				nameToken.Value = new StringView("Function");
				if (CheckForOptionalParts(this._specifierContext.PropertySettings, ref nameToken))
				{
					nameToken = this.TokenReader.GetIdentifier("function name");
				}
				else
				{
					if (this._currentTypeTokens.Count < 2 || !this._currentTypeTokens[^1].IsIdentifier())
					{
						throw new UhtException(this.TokenReader, $"{this._specifierContext.PropertySettings.PropertyCategory.GetHintText()}: Expected name");
					}
					nameToken = this._currentTypeTokens[^1];
					this._currentTypeTokens.RemoveAt(this._currentTypeTokens.Count - 1);
				}
				Finalize(ref nameToken, new ReadOnlyMemory<UhtToken>(this._currentTypeTokens.ToArray()), layoutMacroType, propertyDelegate);
			}
			else if (this._options.HasAnyFlags(UhtPropertyParseOptions.NameIncluded))
			{
				if (this._currentTypeTokens.Count < 2 || !this._currentTypeTokens[^1].IsIdentifier())
				{
					throw new UhtException(this.TokenReader, $"{this._specifierContext.PropertySettings.PropertyCategory.GetHintText()}: Expected name");
				}
				UhtToken nameToken = this._currentTypeTokens[^1];
				this._currentTypeTokens.RemoveAt(this._currentTypeTokens.Count - 1);
				CheckForOptionalParts(this._specifierContext.PropertySettings, ref nameToken);
				Finalize(ref nameToken, new ReadOnlyMemory<UhtToken>(this._currentTypeTokens.ToArray()), layoutMacroType, propertyDelegate);
			}
			else
			{
				UhtToken nameToken = new();
				CheckForOptionalParts(this._specifierContext.PropertySettings, ref nameToken);
				Finalize(ref nameToken, new ReadOnlyMemory<UhtToken>(this._currentTypeTokens.ToArray()), layoutMacroType, propertyDelegate);
			}
		}

		/// <summary>
		/// Finish creating the property
		/// </summary>
		/// <param name="nameToken">The name of the property</param>
		/// <param name="typeTokens">Series of tokens that represent the type</param>
		/// <param name="layoutMacroType">Optional layout macro type being parsed</param>
		/// <param name="propertyDelegate">Delegate to invoke when processing has been completed</param>
		/// <returns>The newly created property.  During the parsing phase, this will often be a temporary property if the type references engine types.</returns>
		private UhtProperty Finalize(ref UhtToken nameToken, ReadOnlyMemory<UhtToken> typeTokens, UhtLayoutMacroType layoutMacroType, UhtPropertyDelegate propertyDelegate)
		{
			this._specifierContext.PropertySettings.SourceName = this._specifierContext.PropertySettings.PropertyCategory == UhtPropertyCategory.Return ? "ReturnValue" : nameToken.Value.ToString();

			// Try to resolve the property using any immediate mode property types
			UhtProperty? newProperty = ResolveProperty(UhtPropertyResolvePhase.Parsing, this._specifierContext.PropertySettings, this._specifierContext.PropertySettings.Outer.HeaderFile.Data.Memory, typeTokens);
			if (newProperty == null)
			{
				newProperty = new UhtPreResolveProperty(this._specifierContext.PropertySettings, typeTokens);
			}

			// Force the category in non-engine projects
			if (newProperty.PropertyCategory == UhtPropertyCategory.Member)
			{
				if (!newProperty.Package.IsPartOfEngine &&
					newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.BlueprintVisible) &&
					!newProperty.MetaData.ContainsKey(UhtNames.Category))
				{
					newProperty.MetaData.Add(UhtNames.Category, newProperty.Outer!.EngineName);
				}
			}

			// Check to see if the variable is deprecated, and if so set the flag
			{
				int deprecatedIndex = newProperty.SourceName.IndexOf("_DEPRECATED", StringComparison.Ordinal);
				int nativizedPropertyPostfixIndex = newProperty.SourceName.IndexOf("__pf", StringComparison.Ordinal); //@TODO: check OverrideNativeName in Meta Data, to be sure it's not a random occurrence of the "__pf" string.
				bool ignoreDeprecatedWord = (nativizedPropertyPostfixIndex != -1) && (nativizedPropertyPostfixIndex > deprecatedIndex);
				if ((deprecatedIndex != -1) && !ignoreDeprecatedWord)
				{
					if (deprecatedIndex != newProperty.SourceName.Length - 11)
					{
						this.TokenReader.LogError("Deprecated variables must end with _DEPRECATED");
					}

					// We allow deprecated properties in blueprints that have getters and setters assigned as they may be part of a backwards compatibility path
					bool blueprintVisible = newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintVisible);
					bool warnOnGetter = blueprintVisible && !newProperty.MetaData.ContainsKey(UhtNames.BlueprintGetter);
					bool warnOnSetter = blueprintVisible && !newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly) && !newProperty.MetaData.ContainsKey(UhtNames.BlueprintSetter);

					if (warnOnGetter)
					{
						this.TokenReader.LogWarning($"{newProperty.PropertyCategory.GetHintText()}: Deprecated property '{newProperty.SourceName}' should not be marked as blueprint visible without having a BlueprintGetter");
					}

					if (warnOnSetter)
					{
						this.TokenReader.LogWarning($"{newProperty.PropertyCategory.GetHintText()}: Deprecated property '{newProperty.SourceName}' should not be marked as blueprint writable without having a BlueprintSetter");
					}

					// Warn if a deprecated property is visible
					if (newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.EditConst) || // Property is marked as editable
						(!blueprintVisible && newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly) &&
						!newProperty.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.ImpliedBlueprintPure))) // Is BPRO, but not via Implied Flags and not caught by Getter/Setter path above
					{
						this.TokenReader.LogWarning($"{newProperty.PropertyCategory.GetHintText()}: Deprecated property '{newProperty.SourceName}' should not be marked as visible or editable");
					}

					newProperty.PropertyFlags |= EPropertyFlags.Deprecated;
					newProperty.EngineName = newProperty.SourceName[..deprecatedIndex];
				}
			}

			// Try gathering metadata for member fields
			if (newProperty.PropertyCategory == UhtPropertyCategory.Member)
			{
				UhtSpecifierParser specifiers = UhtSpecifierParser.GetThreadInstance(this._specifierContext, newProperty.SourceName, this.TopScope.Session.GetSpecifierTable(UhtTableNames.PropertyMember));
				specifiers.ParseFieldMetaData();
				this.TopScope.AddFormattedCommentsAsTooltipMetaData(newProperty);
			}

			propertyDelegate(this.TopScope, newProperty, ref nameToken, layoutMacroType);

			// Void properties don't get added when they are the return value
			if (newProperty.PropertyCategory != UhtPropertyCategory.Return || !this._options.HasAnyFlags(UhtPropertyParseOptions.DontAddReturn))
			{
				this.TopScope.ScopeType.AddChild(newProperty);
			}
			return newProperty;
		}

		private bool CheckForOptionalParts(UhtPropertySettings propertySettings, ref UhtToken nameToken)
		{
			bool gotOptionalParts = false;

			if (this.TokenReader.TryOptional('['))
			{
				RequireArray(propertySettings, ref nameToken, ']');
				this.TokenReader.Require(']');
				gotOptionalParts = true;
			}

			if (this.TokenReader.TryOptional(':'))
			{
				RequireBitfield(propertySettings, ref nameToken);
				gotOptionalParts = true;
			}
			return gotOptionalParts;
		}

		private void RequireBitfield(UhtPropertySettings propertySettings, ref UhtToken nameToken)
		{
			if (!this.TokenReader.TryOptionalConstInt(out int bitfieldSize) || bitfieldSize != 1)
			{
				throw new UhtException(this.TokenReader, $"Bad or missing bit field size for '{nameToken.Value}', must be 1.");
			}
			propertySettings.IsBitfield = true;
		}

		private void RequireArray(UhtPropertySettings propertySettings, ref UhtToken nameToken, char terminator)
		{
			// Ignore how the actual array dimensions are actually defined - we'll calculate those with the compiler anyway.
			propertySettings.ArrayDimensions = this.TokenReader.GetRawString(terminator, UhtRawStringOptions.DontConsumeTerminator).ToString();
			if (propertySettings.ArrayDimensions.Length == 0)
			{
				throw new UhtException(this.TokenReader, $"{propertySettings.PropertyCategory.GetHintText()} {nameToken.Value}: Missing array dimensions or terminating '{terminator}'");
			}
		}

		private bool GatherTypeTokens(ref UhtToken token)
		{
			if (this._currentTemplateDepth == 0 && token.IsSymbol() && (token.IsValue(',') || token.IsValue('(') || token.IsValue(')') || token.IsValue(';') || token.IsValue('[') || token.IsValue(':') || token.IsValue('=') || token.IsValue('{')))
			{
				return false;
			}

			this._currentTypeTokens.Add(token);
			if (token.IsSymbol('<'))
			{
				++this._currentTemplateDepth;
			}
			else if (token.IsSymbol('>'))
			{
				if (this._currentTemplateDepth == 0)
				{
					throw new UhtTokenException(TokenReader, token, "',' or ')'");
				}
				--this._currentTemplateDepth;
			}
			return true;
		}
	}

	[UnrealHeaderTool]
	static class PropertyKeywords
	{
		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Class)]
		[UhtKeyword(Extends = UhtTableNames.ScriptStruct)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtParseResult UPROPERTYKeyword(UhtParsingScope topScope, UhtParsingScope actionScope, ref UhtToken token)
		{
			UhtPropertyParseOptions options = UhtPropertyParseOptions.ParseLayoutMacro | UhtPropertyParseOptions.List | UhtPropertyParseOptions.AddModuleRelativePath;
			topScope.HeaderParser.GetCachedPropertyParser(topScope).Parse(EPropertyFlags.ParmFlags, options, UhtParsePropertyDeclarationStyle.UPROPERTY, UhtPropertyCategory.Member, s_propertyDelegate);
			topScope.TokenReader.Require(';');
			return UhtParseResult.Handled;
		}
		#endregion

		private static readonly UhtPropertyDelegate s_propertyDelegate = PropertyParsed;

		private static void PropertyParsed(UhtParsingScope topScope, UhtProperty property, ref UhtToken nameToken, UhtLayoutMacroType layoutMacroType)
		{
			IUhtTokenReader tokenReader = topScope.TokenReader;

			// Skip any initialization
			if (tokenReader.TryOptional('='))
			{
				tokenReader.SkipUntil(';');
			}
			else if (tokenReader.TryOptional('{'))
			{
				tokenReader.SkipBrackets('{', '}', 1);
			}
		}
	}

	[UnrealHeaderTool]
	static class UhtDefaultPropertyParser
	{
		[UhtPropertyType(Options = UhtPropertyTypeOptions.Default)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtProperty? DefaultProperty(UhtPropertyResolvePhase resolvePhase, UhtPropertySettings propertySettings, IUhtTokenReader tokenReader, UhtToken matchedToken)
		{
			UhtSession session = propertySettings.Outer.Session;
			int typeStartPos = tokenReader.PeekToken().InputStartPos;

			if (tokenReader.TryOptional("const"))
			{
				propertySettings.MetaData.Add(UhtNames.NativeConst, "");
			}

			UhtFindOptions findOptions = UhtFindOptions.DelegateFunction | UhtFindOptions.Enum | UhtFindOptions.Class | UhtFindOptions.ScriptStruct;
			if (tokenReader.TryOptional("enum"))
			{
				if (propertySettings.PropertyCategory == UhtPropertyCategory.Member)
				{
					tokenReader.LogError($"Cannot declare enum at variable declaration");
				}
				findOptions = UhtFindOptions.Enum;
			}
			else if (tokenReader.TryOptional("class"))
			{
				findOptions = UhtFindOptions.Class;
			}
			else if (tokenReader.TryOptional("struct"))
			{
				findOptions = UhtFindOptions.ScriptStruct;
			}

			UhtTokenList identifiers = tokenReader.GetCppIdentifier();
			UhtType? type = propertySettings.Outer.FindType(UhtFindOptions.SourceName | findOptions, identifiers, tokenReader);
			if (type == null)
			{
				return null;
			}
			UhtTokenListCache.Return(identifiers);

			if (type is UhtEnum enumObj)
			{
				if (propertySettings.PropertyCategory == UhtPropertyCategory.Member)
				{
					if (enumObj.CppForm != UhtEnumCppForm.EnumClass)
					{
						tokenReader.LogError("You cannot use the raw enum name as a type for member variables, instead use TEnumAsByte or a C++11 enum class with an explicit underlying type.");
					}
				}

				return new UhtEnumProperty(propertySettings, enumObj);
			}
			else if (type is UhtScriptStruct scriptStruct)
			{
				return new UhtStructProperty(propertySettings, scriptStruct);
			}
			else if (type is UhtFunction function)
			{
				if (!function.FunctionFlags.HasAnyFlags(EFunctionFlags.MulticastDelegate))
				{
					return new UhtDelegateProperty(propertySettings, function);
				}
				else if (function.FunctionType == UhtFunctionType.SparseDelegate)
				{
					return new UhtMulticastSparseDelegateProperty(propertySettings, function);
				}
				else
				{
					return new UhtMulticastInlineDelegateProperty(propertySettings, function);
				}
			}
			else if (type is UhtClass classObj)
			{
				// Const after variable type but before pointer symbol
				if (tokenReader.TryOptional("const"))
				{
					propertySettings.MetaData.Add(UhtNames.NativeConst, "");
				}
				tokenReader.Require('*');

				// Optionally emit messages about native pointer members and swallow trailing 'const' after pointer properties
				UhtObjectPropertyBase.ConditionalLogPointerUsage(propertySettings, session.Config!.EngineNativePointerMemberBehavior,
					session.Config!.NonEngineNativePointerMemberBehavior, "Native pointer", tokenReader, typeStartPos, "TObjectPtr");

				if (propertySettings.PropertyCategory == UhtPropertyCategory.Member)
				{
					tokenReader.TryOptional("const");
				}

				propertySettings.PointerType = UhtPointerType.Native;

				if (classObj.ClassFlags.HasAnyFlags(EClassFlags.Interface))
				{
					return new UhtInterfaceProperty(propertySettings, classObj);
				}
				else if (classObj.IsChildOf(session.UClass))
				{
					// UObject here specifies that there is no limiter
					return new UhtClassProperty(propertySettings, classObj, session.UObject);
				}
				else
				{
					return new UhtObjectProperty(propertySettings, classObj);
				}
			}
			else
			{
				throw new UhtIceException("Unexpected type found");
			}
		}
	}
}
