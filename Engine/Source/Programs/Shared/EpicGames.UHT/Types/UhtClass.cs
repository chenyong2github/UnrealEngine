// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// Series of flags not part of the engine's class flags that affect code generation or verification
	/// </summary>
	[Flags]
	public enum UhtClassExportFlags : UInt32
	{
		None = 0,

		/// <summary>
		/// If set, the class itself has replicated properties.
		/// </summary>
		SelfHasReplicatedProperties = 1 << 0,

		/// <summary>
		/// If set, some super class has replicated properties.
		/// </summary>
		SuperHasReplicatedProperties = 1 << 1,

		/// <summary>
		/// If set, either the class itself or a super class has replicated properties
		/// </summary>
		HasReplciatedProperties = SelfHasReplicatedProperties | SuperHasReplicatedProperties,

		HasCustomConstructor = 1 << 2,

		HasDefaultConstructor = 1 << 3,

		HasObjectInitializerConstructor = 1 << 4,

		HasCustomVTableHelperConstructor = 1 << 5,

		HasConstructor = 1 << 6,

		HasGetLifetimeReplicatedProps = 1 << 7,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtClassExportFlagsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtClassExportFlags InFlags, UhtClassExportFlags TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtClassExportFlags InFlags, UhtClassExportFlags TestFlags)
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
		public static bool HasExactFlags(this UhtClassExportFlags InFlags, UhtClassExportFlags TestFlags, UhtClassExportFlags MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	public enum UhtClassType
	{
		Class,
		Interface,
		NativeInterface,
	}

	[Flags]
	public enum UhtSerializerArchiveType
	{
		None = 0,
		Archive = 1 << 0,
		StructuredArchiveRecord = 1 << 1,
		All = Archive | StructuredArchiveRecord,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtSerializerArchiveTypeExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtSerializerArchiveType InFlags, UhtSerializerArchiveType TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtSerializerArchiveType InFlags, UhtSerializerArchiveType TestFlags)
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
		public static bool HasExactFlags(this UhtSerializerArchiveType InFlags, UhtSerializerArchiveType TestFlags, UhtSerializerArchiveType MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	public struct UhtDeclaration
	{
		public UhtCompilerDirective CompilerDirectives;
		public UhtToken[] Tokens;
	}

	public struct UhtFoundDeclaration
	{
		public UhtCompilerDirective CompilerDirectives;
		public UhtToken[] Tokens;
		public int NameTokenIndex;
		public bool bIsVirtual;
	}

	[UhtEngineClass(Name = "Class")]
	public class UhtClass : UhtStruct
	{
		private static UhtSpecifierValidatorTable ClassSpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.Class);
		private static UhtSpecifierValidatorTable InterfaceSpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.Interface);
		private static UhtSpecifierValidatorTable NativeInterfaceSpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.NativeInterface);

		public string Config { get; set; } = String.Empty;
		public string EnclosingDefine { get; set; } = String.Empty;

		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtClass>))]
		public UhtClass ClassWithin { get; set; }

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EClassFlags ClassFlags { get; set; } = EClassFlags.None;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EClassCastFlags ClassCastFlags { get; set; } = EClassCastFlags.None;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtClassExportFlags ClassExportFlags { get; set; } = UhtClassExportFlags.None;

		public UhtClassType ClassType = UhtClassType.Class;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtSerializerArchiveType SerializerArchiveType { get; set; } = UhtSerializerArchiveType.None;

		public int PrologLineNumber { get; set; } = -1;

		[JsonIgnore]
		public IList<UhtDeclaration>? Declarations => this.DeclarationsInternal;
		public UhtClass? NativeInterface = null;

		public int GeneratedBodyLineNumber { get; set; } = -1;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtAccessSpecifier GeneratedBodyAccessSpecifier { get; set; } = UhtAccessSpecifier.None;

		public bool bHasGeneratedBody = false; // If not true, then GENERATED_UCLASS_BODY was used

		[JsonIgnore]
		public override UhtEngineType EngineType
		{
			get
			{
				switch (this.ClassType)
				{
					case UhtClassType.Class:
						return UhtEngineType.Class;
					case UhtClassType.Interface:
						return UhtEngineType.Interface;
					case UhtClassType.NativeInterface:
						return UhtEngineType.NativeInterface;
					default:
						throw new UhtIceException("Unknown class type");
				}
			}
		}

		/// <inheritdoc/>
		public override string EngineClassName { get => "Class"; }

		///<inheritdoc/>
		[JsonIgnore]
		public override bool bDeprecated => this.ClassFlags.HasAnyFlags(EClassFlags.Deprecated);

		///<inheritdoc/>
		[JsonIgnore]
		protected override UhtSpecifierValidatorTable? SpecifierValidatorTable
		{
			get
			{
				switch (this.ClassType)
				{
					case UhtClassType.Class:
						return UhtClass.ClassSpecifierValidatorTable;
					case UhtClassType.Interface:
						return UhtClass.InterfaceSpecifierValidatorTable;
					case UhtClassType.NativeInterface:
						return UhtClass.NativeInterfaceSpecifierValidatorTable;
					default:
						throw new UhtIceException("Unknown class type");
				}
			}
		}

		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtClass>))]
		public UhtClass? SuperClass => (UhtClass?)this.Super;

		public UhtClass(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
			this.ClassWithin = this;
		}

		[JsonIgnore]
		public bool bIsActorClass => IsChildOf(this.Session.AActor);

		[JsonIgnore]
		public override string EngineNamePrefix
		{ 
			get
			{
				switch (this.ClassType)
				{
					case UhtClassType.Class:  
						if (bIsActorClass)
						{
							return this.ClassFlags.HasAnyFlags(EClassFlags.Deprecated) ? "ADEPRECATED_" : "A";
						}
						else
						{
							return this.ClassFlags.HasAnyFlags(EClassFlags.Deprecated) ? "UDEPRECATED_" : "U";
						}

					case UhtClassType.Interface:
						return "U";

					case UhtClassType.NativeInterface:
						return "I";

					default:
						throw new NotImplementedException();
				}
			}
		}

		private List<UhtDeclaration>? DeclarationsInternal = null;

		/// <summary>
		/// Add the given list of tokens as a possible declaration
		/// </summary>
		/// <param name="CompilerDirectives">Currently active compiler directives</param>
		/// <param name="Tokens">List of declaration tokens</param>
		public void AddDeclaration(UhtCompilerDirective CompilerDirectives, List<UhtToken> Tokens)
		{
			if (this.DeclarationsInternal == null)
			{
				this.DeclarationsInternal = new List<UhtDeclaration>();
			}
			this.DeclarationsInternal.Add(new UhtDeclaration { CompilerDirectives = CompilerDirectives, Tokens = Tokens.ToArray() });
		}

		/// <summary>
		/// Search the declarations for a possible declaration match with the given name
		/// </summary>
		/// <param name="Name">Name to be located</param>
		/// <param name="FoundDeclaration">Information about the matched declaration</param>
		/// <returns>True if a declaration was found with the name.</returns>
		public bool TryGetDeclaration(string Name, out UhtFoundDeclaration FoundDeclaration)
		{
			if (Name.Length > 0)
			{
				if (this.Declarations != null)
				{
					foreach (UhtDeclaration Declaration in this.Declarations)
					{
						bool bIsVirtual = false;
						for (int Index = 0; Index < Declaration.Tokens.Length; ++Index)
						{
							if (Declaration.Tokens[Index].IsIdentifier("virtual"))
							{
								bIsVirtual = true;
							}
							else if (Declaration.Tokens[Index].IsIdentifier(Name))
							{
								FoundDeclaration = new UhtFoundDeclaration
								{
									CompilerDirectives = Declaration.CompilerDirectives,
									Tokens = Declaration.Tokens,
									NameTokenIndex = Index,
									bIsVirtual = bIsVirtual,
								};
								return true;
							}
						}
					}
				}
			}
			if (this.NativeInterface != null)
			{
				return this.NativeInterface.TryGetDeclaration(Name, out FoundDeclaration);
			}
			FoundDeclaration = new UhtFoundDeclaration();
			return false;
		}

		public IEnumerable<UhtProperty> EnumerateReplicatedProperties(bool bIncludeSuper)
		{
			if (bIncludeSuper && this.SuperClass != null)
			{
				foreach (UhtProperty Property in this.SuperClass.EnumerateReplicatedProperties(true))
				{
					yield return Property;
				}
			}

			foreach (UhtProperty Property in this.Properties)
			{
				if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
				{
					yield return Property;
				}
			}
		}

		/// <summary>
		/// Checks to see if the class or any super class has the given flags.
		/// </summary>
		/// <param name="FlagsToCheck"></param>
		/// <returns>True if the flags are found</returns>
		public bool HierarchyHasAnyClassFlags(EClassFlags FlagsToCheck)
		{
			for (UhtClass? Current = this; Current != null; Current = Current.SuperClass)
			{
				if (Current.ClassFlags.HasAnyFlags(FlagsToCheck))
				{
					return true;
				}
			}
			return false;
		}

		#region Resolution support
		struct GetterSetterToResolve
		{
			public UhtProperty Property;
			public bool bSetter;
			public bool bFound;
		}

		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResult = base.ResolveSelf(Phase);

			switch (Phase)
			{
				case UhtResolvePhase.Bases:

					// Check to see if this class matches an entry in the ClassCastFlags.  If it does, that
					// becomes our ClassCastFlags.  If not, then we inherit the flags from the super.
					if (Enum.TryParse<EClassCastFlags>(this.SourceName, false, out EClassCastFlags Found))
					{
						if (Found != EClassCastFlags.None)
						{
							this.ClassCastFlags = Found;
						}
					}
					if (this.ClassCastFlags == EClassCastFlags.None && this.SuperClass != null)
					{
						this.ClassCastFlags = this.SuperClass.ClassCastFlags;
					}

					// force members to be 'blueprint read only' if in a const class
					// This must be done after the Const flag has been propagated from the parent
					// and before properties have been finalized.
					if (this.ClassType == UhtClassType.Class && this.ClassFlags.HasAnyFlags(EClassFlags.Const))
					{
						foreach (UhtType Child in this.Children)
						{
							if (Child is UhtProperty Property)
							{
								Property.PropertyFlags |= EPropertyFlags.BlueprintReadOnly;
								Property.PropertyExportFlags |= UhtPropertyExportFlags.ImpliedBlueprintPure;
							}
						}
					}
					break;

				case UhtResolvePhase.Final:
					bool bAutoGetterSetter = UhtConfig.Instance.bAllowAutomaticSettersAndGetters;
					Dictionary<string, List<GetterSetterToResolve>>? GSToResolve = null;
					foreach (UhtType Child in this.Children)
					{
						if (Child is UhtProperty Property)
						{
							if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
							{
								this.ClassExportFlags |= UhtClassExportFlags.SelfHasReplicatedProperties;
								break;
							}

							if (Property.SourceName == "SourceModels")
							{
								Debugger.Break();
							}

							if (!Property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterSpecifiedNone))
							{
								if (Property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterSpecified))
								{
									GSToResolve = AddGetterSetter(GSToResolve, Property.Getter == null ? $"Get{Property.SourceName}" : Property.Getter, Property, false);
								}
								else if (bAutoGetterSetter)
								{
									GSToResolve = AddGetterSetter(GSToResolve, $"Get{Property.SourceName}", Property, false);
								}
							}

							if (!Property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.SetterSpecifiedNone))
							{
								if (Property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.SetterSpecified))
								{
									GSToResolve = AddGetterSetter(GSToResolve, Property.Setter == null ? $"Set{Property.SourceName}" : Property.Setter, Property, true);
								}
								else if (bAutoGetterSetter)
								{
									GSToResolve = AddGetterSetter(GSToResolve, $"Set{Property.SourceName}", Property, true);
								}
							}
						}
					}

					if (this.SuperClass != null)
					{
						if (this.SuperClass.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.HasReplciatedProperties))
						{
							this.ClassExportFlags |= UhtClassExportFlags.SuperHasReplicatedProperties;
						}
					}

					if (!this.ClassFlags.HasAnyFlags(EClassFlags.Interface))
					{
						if (this.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.SelfHasReplicatedProperties))
						{
							if (TryGetDeclaration("GetLifetimeReplicatedProps", out UhtFoundDeclaration FoundDeclaration))
							{
								this.ClassExportFlags |= UhtClassExportFlags.HasGetLifetimeReplicatedProps;
							}
						}
					}

					// If we have any getters/setters to resolve 
					if (GSToResolve != null)
					{
						if (this.Declarations != null)
						{
							foreach (UhtDeclaration Declaration in this.Declarations)
							{

								// Locate the index of the name
								int NameIndex = 0;
								for (; NameIndex < Declaration.Tokens.Length - 1; ++NameIndex)
								{
									if (Declaration.Tokens[NameIndex + 1].IsValue('('))
									{
										break;
									}
								}

								string Name = Declaration.Tokens[NameIndex].Value.ToString();
								if (GSToResolve.TryGetValue(Name, out List<GetterSetterToResolve>? GSList))
								{
									for (int Index = 0; Index < GSList.Count; ++Index)
									{
										GetterSetterToResolve GS = GSList[Index];
										if (!GS.bFound)
										{
											if (TryMatchGetterSetter(GS.Property, GS.bSetter, Declaration, NameIndex))
											{
												GS.bFound = true;
												GSList[Index] = GS;
												GS.Property.PropertyExportFlags |= GS.bSetter ? UhtPropertyExportFlags.SetterFound : UhtPropertyExportFlags.GetterFound;
												if (GS.bSetter)
												{
													GS.Property.Setter = Name;
												}
												else
												{
													GS.Property.Getter = Name;
												}
											}
										}
									}
								}
							}
						}
					}
					break;
			}

			return bResult;
		}

		/// <inheritdoc/>
		protected override void ResolveChildren(UhtResolvePhase Phase)
		{
			base.ResolveChildren(Phase);

			switch (Phase)
			{
				case UhtResolvePhase.Final:
					if (ScanForInstancedReferenced(false))
					{
						this.ClassFlags |= EClassFlags.HasInstancedReference;
					}
					break;
			}
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool bDeepScan)
		{
			if (this.ClassFlags.HasAnyFlags(EClassFlags.HasInstancedReference))
			{
				return true;
			}

			if (this.SuperClass != null && this.SuperClass.ScanForInstancedReferenced(bDeepScan))
			{
				return true;
			}

			return base.ScanForInstancedReferenced(bDeepScan);
		}

		/// <summary>
		/// Given a token stream, verify that it matches the expected signature for a getter/setter
		/// </summary>
		/// <param name="Property">Property requesting the setter/getter</param>
		/// <param name="bSetter">If true, a setter is expected</param>
		/// <param name="Declaration">The declaration being tested</param>
		/// <param name="NameIndex">The index of the token with the expected getter/setter name</param>
		/// <returns>True if the declaration matches</returns>
		private bool TryMatchGetterSetter(UhtProperty Property, bool bSetter, UhtDeclaration Declaration, int NameIndex)
		{
			UhtToken[] Tokens = Declaration.Tokens;
			int TokenIndex = 0;
			int TokenCount = Tokens.Length;

			// Skip all of the API and virtual keywords
			for (; TokenIndex < TokenCount && (Tokens[TokenIndex].IsValue("virtual") || Tokens[TokenIndex].Value.Span.EndsWith("_API")); ++TokenIndex)
			{
				// Nothing to do in the body
			}

			int TypeIndex = 0;
			int TypeCount = 0;

			// Verify the format of the function declaration and extract the type
			if (bSetter)
			{

				// The return type must be 'void'
				if (TokenIndex == TokenCount || !Tokens[TokenIndex].IsValue("void"))
				{
					return false;
				}
				TokenIndex++;

				// Followed by the name
				if (TokenIndex == TokenCount || TokenIndex != NameIndex)
				{
					return false;
				}
				TokenIndex++;

				// Followed by the '('
				if (TokenIndex == TokenCount || !Tokens[TokenIndex].IsValue('('))
				{
					return false;
				}
				TokenIndex++;

				// Find the ')'
				TypeIndex = TokenIndex;
				while (true)
				{
					if (TokenIndex == TokenCount)
					{
						return false;
					}
					if (Tokens[TokenIndex].IsValue(')'))
					{
						TypeCount = TokenIndex - TypeIndex;
						break;
					}
					TokenIndex++;
				}
			}
			else
			{
				// Get the type range
				TypeIndex = TokenIndex;
				TypeCount = NameIndex - TypeIndex;
				TokenIndex = NameIndex + 1;

				// Followed by the '('
				if (TokenIndex == TokenCount || !Tokens[TokenIndex].IsValue('('))
				{
					return false;
				}
				TokenIndex++;

				// Followed by the ')'
				if (TokenIndex == TokenCount || !Tokens[TokenIndex].IsValue(')'))
				{
					return false;
				}
				TokenIndex++;

				// Followed by the "const"
				if (TokenIndex == TokenCount || !Tokens[TokenIndex].IsValue("const"))
				{
					return false;
				}
				TokenIndex++;
			}

			// Verify the type
			using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Small))
			{
				StringBuilder Builder = Borrower.StringBuilder;
				Builder.AppendPropertyText(Property, UhtPropertyTextType.GetterSetterArg);
				string ExpectedType = Builder.ToString();
				int TypeEnd = TypeIndex + TypeCount;

				// We will do a somewhat brute force string compare to avoid constructing an expected
				// string or parsing the required string
				ReadOnlySpan<char> ExpectedSpan = ExpectedType.AsSpan().Trim();

				// skip const. We allow const in parameter and return values even if the property is not const
				if (TypeIndex < TypeEnd && Tokens[TypeIndex].IsIdentifier("const"))
				{
					TypeIndex++;
				}

				// Loop until we are done
				while (TypeIndex < TypeEnd && ExpectedSpan.Length > 0)
				{
					UhtToken Token = Tokens[TypeIndex];
					ReadOnlySpan<char> TokenSpan = Token.Value.Span;

					// Make sure we match the expected type
					if (TokenSpan.Length > ExpectedSpan.Length ||
						!ExpectedSpan.StartsWith(TokenSpan))
					{
						return false;
					}

					// Extract the remaining part of the string we expect to match
					ExpectedSpan = ExpectedSpan.Slice(TokenSpan.Length).TrimStart();
					TypeIndex++;
				}

				// Consume any '&' for reference support
				if (TypeIndex < TypeEnd && Tokens[TypeIndex].IsSymbol('&'))
				{
					TypeIndex++;
				}

				// For setters, there can be an identifier
				if (bSetter && TypeIndex < TypeEnd && Tokens[TypeIndex].IsIdentifier())
				{
					TypeIndex++;
				}

				// We must be at the end for a match
				if (TypeIndex != TypeEnd)
				{
					return false;
				}
			}

			// Validate the #if blocks.  This should really be done during validation
			string FuncType = bSetter ? "setter" : "getter";
			if (Declaration.CompilerDirectives.HasAnyFlags(UhtCompilerDirective.WithEditor))
			{
				if (Property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterSpecified))
				{
					Property.LogError(Tokens[0].InputLine, $"Property {Property.SourceName} {FuncType} function {Tokens[NameIndex].Value.ToString()} "
						+ "cannot be declared within WITH_EDITOR block. Use WITH_EDITORONLY_DATA instead.");
				}
				return false;
			}
			if (Declaration.CompilerDirectives.HasAnyFlags(UhtCompilerDirective.WithEditorOnlyData) && !Property.PropertyFlags.HasAnyFlags(EPropertyFlags.EditorOnly))
			{
				if (Property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterSpecified))
				{
					Property.LogError(Tokens[0].InputLine, $"Property {Property.SourceName} is not editor-only but its {FuncType} function '{Tokens[NameIndex].Value.ToString()}' is");
				}
				return false;
			}
			return true;
		}

		/// <summary>
		/// Add a requested getter/setter function name
		/// </summary>
		/// <param name="GSToResolve">Dictionary containing the lookup by function name.  Will be created if null.</param>
		/// <param name="Name">Name of the getter/setter</param>
		/// <param name="Property">Property requesting the getter/setter</param>
		/// <param name="bSetter">True if this is a setter</param>
		/// <returns>Resulting dictionary</returns>
		private Dictionary<string, List<GetterSetterToResolve>> AddGetterSetter(Dictionary<string, List<GetterSetterToResolve>>? GSToResolve, string Name, UhtProperty Property, bool bSetter)
		{
			if (GSToResolve == null)
			{
				GSToResolve = new Dictionary<string, List<GetterSetterToResolve>>();
			}
			List<GetterSetterToResolve>? GSList = null;
			if (!GSToResolve.TryGetValue(Name, out GSList))
			{
				GSList = new List<GetterSetterToResolve>();
				GSToResolve.Add(Name, GSList);
			}
			GSList.Add(new GetterSetterToResolve { Property = Property, bSetter = bSetter });
			return GSToResolve;
		}
		#endregion

		#region Validation support
		protected override UhtValidationOptions Validate(UhtValidationOptions Options)
		{
			Options = base.Validate(Options);

			// Classes must start with a valid prefix
			string ExpectedClassName = this.EngineNamePrefix + this.EngineName;
			if (ExpectedClassName != this.SourceName)
			{
				this.LogError($"Class '{this.SourceName}' has an invalid Unreal prefix, expecting '{ExpectedClassName}'");
			}

			// If we have a super class
			if (this.SuperClass != null)
			{
				if (!this.ClassFlags.HasAnyFlags(EClassFlags.NotPlaceable) && this.SuperClass.ClassFlags.HasAnyFlags(EClassFlags.NotPlaceable))
				{
					this.LogError("The 'placeable' specifier cannot override a 'nonplaceable' base class. Classes are assumed to be placeable by default. "
						+ "Consider whether using the 'abstract' specifier on the base class would work.");
				}

				// Native interfaces don't require a super class, but UHT technically allows it.
				if (this.ClassType == UhtClassType.Interface || this.ClassType == UhtClassType.NativeInterface)
				{
					if (this != this.Session.UInterface && !this.SuperClass.ClassFlags.HasAnyFlags(EClassFlags.Interface))
					{
						this.LogError($"Interface class '{this.SourceName}' cannot inherit from non-interface class '{this.SuperClass.SourceName}'");
					}
				}
			}

			// If we don't have a super class
			else
			{
				if (this.ClassType == UhtClassType.Interface)
				{
					this.LogError($"Interface '{this.SourceName}' must derive from an interface");
				}
			}

			if (this.ClassFlags.HasAnyFlags(EClassFlags.NeedsDeferredDependencyLoading) && !this.IsChildOf(this.Session.UClass))
			{
				// CLASS_NeedsDeferredDependencyLoading can only be set on classes derived from UClass
				this.LogError($"'NeedsDeferredDependencyLoading' is set on '{this.SourceName}' but the flag can only be used with classes derived from UClass.");
			}

			ValidateProperties();

			// Perform any validation specific to the class type
			switch (this.ClassType)
			{
				case UhtClassType.Class:
					{
						if (this.ClassFlags.HasAnyFlags(EClassFlags.EditInlineNew))
						{
							// don't allow actor classes to be declared EditInlineNew
							if (this.IsChildOf(this.Session.AActor))
							{
								this.LogError("Invalid class attribute: Creating actor instances via the property window is not allowed");
							}
						}

						// Make sure both RequiredAPI and MinimalAPI aren't specified
						if (this.ClassFlags.HasAllFlags(EClassFlags.MinimalAPI | EClassFlags.RequiredAPI))
						{
							this.LogError("MinimalAPI cannot be specified when the class is fully exported using a MODULENAME_API macro");
						}

						// Make sure that all interface functions are implemented property
						if (this.Bases != null)
						{

							// Iterate over all the bases looking for any interfaces
							foreach (UhtStruct BaseStruct in this.Bases)
							{

								// Skip children that aren't interfaces or if a common ancestor
								UhtClass? BaseClass = BaseStruct as UhtClass;
								if (BaseClass == null || BaseClass.ClassType == UhtClassType.Class || IsChildOf(BaseClass) )
								{
									continue;
								}

								// Loop through the function to make sure they are implemented
								List<UhtType> BaseChildren = BaseClass.AlternateObject != null ? BaseClass.AlternateObject.Children : BaseClass.Children;
								foreach (UhtType BaseChild in BaseChildren)
								{
									UhtFunction? BaseFunction = BaseChild as UhtFunction;
									if (BaseFunction == null)
									{
										continue; 
									}

									// Delegate signature functions are simple stubs and aren't required to be implemented (they are not callable)
									bool bImplemented = BaseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate);

									// Try to find an existing function
									foreach (UhtType Child in this.Children)
									{
										UhtFunction? Function = Child as UhtFunction;
										if (Function == null || Function.SourceName != BaseFunction.SourceName)
										{
											continue;
										}

										if (BaseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Event) && !Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
										{
											Function.LogError($"Implementation of function '{Function.SourceName}' must be declared as 'event' to match declaration in interface '{BaseClass.SourceName}'");
										}

										if (BaseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate) && !Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
										{
											Function.LogError($"Implementation of function '{Function.SourceName}' must be declared as 'delegate' to match declaration in interface '{BaseClass.SourceName}'");
										}

										bImplemented = true;

										if (BaseFunction.Children.Count != Function.Children.Count)
										{
											Function.LogError($"Implementation of function '{Function.SourceName}' conflicts with interface '{BaseClass.SourceName}' - different number of parameters ({Function.Children.Count}/{BaseFunction.Children.Count})");
											continue;
										}

										for (int Index = 0; Index < Function.Children.Count; ++Index)
										{
											UhtProperty BaseProperty = (UhtProperty)BaseFunction.Children[Index];
											UhtProperty Property = (UhtProperty)Function.Children[Index];
											if (!BaseProperty.MatchesType(Property))
											{
												if (BaseProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm))
												{
													Function.LogError($"Implementation of function '{Function.SourceName}' conflicts only by return type with interface '{BaseClass.SourceName}'");
												}
												else
												{
													Function.LogError($"Implementation of function '{Function.SourceName}' conflicts type with interface '{BaseClass.SourceName}' - parameter {Index} '{Property.SourceName}'");
												}
											}
										}
									}

									// Verify that if this has blueprint-callable functions that are not implementable events, we've implemented them as a UFunction in the target class
									if (!bImplemented
										&& BaseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable)
										&& !BaseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent)
										&& !BaseClass.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint)
										&& (BaseClass.AlternateObject == null || !BaseClass.AlternateObject.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint)))
									{
										this.LogError($"Missing UFunction implementation of function '{BaseFunction.SourceName}' from interface '{BaseClass.SourceName}'.  This function needs a UFUNCTION() declaration.");
									}
								}
							}
						}
					}

					if (!this.ClassFlags.HasAnyFlags(EClassFlags.Interface))
					{
						if (this.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.SelfHasReplicatedProperties) &&
							!this.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.HasGetLifetimeReplicatedProps) &&
							this.GeneratedCodeVersion != EGeneratedCodeVersion.V1)
						{
							this.LogError($"Class {this.SourceName} has Net flagged properties and should declare member function: void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override");
						}
					}
					break;

				case UhtClassType.Interface:
					{
						// Interface with blueprint data should declare explicitly Blueprintable or NotBlueprintable to be clear
						// In the backward compatible case where they declare neither, both of these bools are false
						bool bCanImplementInBlueprints = this.MetaData.GetBoolean(UhtNames.IsBlueprintBase);
						bool bCannotImplementInBlueprints = (!bCanImplementInBlueprints && this.MetaData.ContainsKey(UhtNames.IsBlueprintBase))
							|| this.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint);

						bool bCanImplementInterfaceInBlueprint = !this.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint);
						foreach (UhtType Child in this.Children)
						{
							if (Child is UhtFunction Function)
							{
								// Verify interfaces with respect to their blueprint accessible functions
								if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent) && !Function.MetaData.GetBoolean(UhtNames.BlueprintInternalUseOnly))
								{
									// Ensure that blueprint events are only allowed in implementable interfaces. Internal only functions allowed
									if (bCannotImplementInBlueprints)
									{
										Function.LogError("Interfaces that are not implementable in blueprints cannot have Blueprint Event members.");
									}
									if (!bCanImplementInBlueprints)
									{
										// We do not currently warn about this case as there are a large number of existing interfaces that do not specify
										// Function.LogWarning(TEXT("Interfaces with Blueprint Events should declare Blueprintable on the interface."));
									}
								}

								if (Function.FunctionFlags.HasExactFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.BlueprintEvent, EFunctionFlags.BlueprintCallable))
								{
									// Ensure that if this interface contains blueprint callable functions that are not blueprint defined, that it must be implemented natively
									if (bCanImplementInBlueprints)
									{
										Function.LogError("Blueprint implementable interfaces cannot contain BlueprintCallable functions that are not "
											+ "BlueprintImplementableEvents. Add NotBlueprintable to the interface if you wish to keep this function.");
									}
									if (!bCannotImplementInBlueprints)
									{
										// Lowered this case to a warning instead of error, they will not show up as blueprintable unless they also have events
										Function.LogWarning("Interfaces with BlueprintCallable functions but no events should explicitly declare NotBlueprintable on the interface.");
									}
								}
							}
						}
					}
					break;

				case UhtClassType.NativeInterface:
					break;
			}

			return Options |= UhtValidationOptions.Shadowing | UhtValidationOptions.Deprecated;
		}

		void ValidateBlueprintPopertyGetter(UhtProperty Property, string FunctionName, UhtFunction? TargetFunction)
		{
			if (TargetFunction == null)
			{
				Property.LogError($"Blueprint Property getter function '{FunctionName}' not found");
				return;
			}

			if (TargetFunction.ParameterProperties.Length != 0)
			{
				TargetFunction.LogError($"Blueprint Property getter function '{TargetFunction.SourceName}' must not have parameters.");
			}

			UhtProperty? ReturnProperty = TargetFunction.ReturnProperty;
			if (ReturnProperty == null || !Property.IsSameType(ReturnProperty))
			{
				TargetFunction.LogError($"Blueprint Property getter function '{TargetFunction.SourceName}' must have return value of type '{Property.GetUserFacingDecl()}'.");
			}

			if (TargetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
			{
				TargetFunction.LogError($"Blueprint Property getter function '{TargetFunction.SourceName}' cannot be a blueprint event.");
			}

			if (!TargetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure))
			{
				TargetFunction.LogError($"Blueprint Property getter function '{TargetFunction.SourceName}' must be pure.");
			}
		}

		void ValidateBlueprintPopertySetter(UhtProperty Property, string FunctionName, UhtFunction? TargetFunction)
		{
			if (TargetFunction == null)
			{
				Property.LogError($"Blueprint Property setter function '{FunctionName}' not found");
				return;
			}

			if (TargetFunction.ReturnProperty != null)
			{
				TargetFunction.LogError($"Blueprint Property setter function '{TargetFunction.SourceName}' must not have a return value.");
			}

			if (TargetFunction.ParameterProperties.Length != 1 || !Property.IsSameType((UhtProperty)TargetFunction.ParameterProperties.Span[0]))
			{
				TargetFunction.LogError($"Blueprint Property setter function '{TargetFunction.SourceName}' must have exactly one parameter of type '{Property.GetUserFacingDecl()}'.");
			}

			if (TargetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
			{
				TargetFunction.LogError($"Blueprint Property setter function '{TargetFunction.SourceName}' cannot be a blueprint event.");
			}

			if (!TargetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable))
			{
				TargetFunction.LogError($"Blueprint Property setter function '{TargetFunction.SourceName}' must be a blueprint callable.");
			}

			if (TargetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure))
			{
				TargetFunction.LogError($"Blueprint Property setter function '{TargetFunction.SourceName}' must not be pure.");
			}
		}

		void ValidateRepNotifyCallback(UhtProperty Property, string FunctionName, UhtFunction? TargetFunction)
		{
			if (TargetFunction == null)
			{
				Property.LogError($"Replication notification function '{FunctionName}' not found");
				return;
			}

			if (TargetFunction.ReturnProperty != null)
			{
				TargetFunction.LogError($"Replication notification function '{TargetFunction.SourceName}' must not have a return value.");
			}

			bool bIsArrayProperty = Property.bIsStaticArray || Property is UhtArrayProperty;
			int MaxParms = bIsArrayProperty ? 2 : 1;
			ReadOnlyMemory<UhtType> Parameters = TargetFunction.ParameterProperties;

			if (Parameters.Length > MaxParms)
			{
				TargetFunction.LogError($"Replication notification function '{TargetFunction.SourceName}' has too many parameters.");
			}

			if (Parameters.Length >= 1)
			{
				UhtProperty Parm = (UhtProperty)Parameters.Span[0];
				// First parameter is always the old value:
				if (!Property.IsSameType(Parm))
				{
					TargetFunction.LogError($"Replication notification function '{TargetFunction.SourceName}' first (optional) parameter must be of type '{Property.GetUserFacingDecl()}'.");
				}
			}

			if (Parameters.Length >= 2)
			{
				UhtProperty Parm = (UhtProperty)Parameters.Span[1];
				// First parameter is always the old value:
				bool bIsValid = false;
				if (Parm is UhtArrayProperty ArrayProperty)
				{
					bIsValid = ArrayProperty.ValueProperty is UhtByteProperty && Parm.PropertyFlags.HasAllFlags(EPropertyFlags.ConstParm | EPropertyFlags.ReferenceParm);
				}
				if (!bIsValid)
				{
					TargetFunction.LogError($"Replication notification function '{TargetFunction.SourceName}' second (optional) parameter must be of type 'const TArray<uint8>&'.");
				}
			}
		}

		void ValidateProperties()
		{
			foreach (UhtType Child in this.Children)
			{
				if (Child is UhtProperty Property)
				{
					if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.RepNotify))
					{
						string Name = Property.RepNotifyName != null ? Property.RepNotifyName : String.Empty;
						ValidateRepNotifyCallback(Property, Name, (UhtFunction?)FindType(UhtFindOptions.EngineName | UhtFindOptions.Function, Name.ToString()));
					}

					if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintVisible))
					{
						if (Property.MetaData.TryGetValue(UhtNames.BlueprintGetter, out string? Getter) && Getter.Length > 0)
						{
							ValidateBlueprintPopertyGetter(Property, Getter, (UhtFunction?)FindType(UhtFindOptions.EngineName | UhtFindOptions.Function, Getter));
						}

						if (!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly))
						{
							if (Property.MetaData.TryGetValue(UhtNames.BlueprintSetter, out string? Setter) && Setter.Length > 0)
							{
								ValidateBlueprintPopertySetter(Property, Setter, (UhtFunction?)FindType(UhtFindOptions.EngineName | UhtFindOptions.Function, Setter));
							}
						}
					}

					// Validate if we are using editor only data in a class or struct definition
					if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.EditorOnly) && this.ClassFlags.HasAnyFlags(EClassFlags.Optional))
					{
						Property.LogError("Cannot specify an editor only property inside an optional class.");
					}

					// Check for getter/setter
					if (Property.PropertyExportFlags.HasExactFlags(UhtPropertyExportFlags.GetterSpecified | UhtPropertyExportFlags.GetterSpecifiedNone | UhtPropertyExportFlags.GetterFound, UhtPropertyExportFlags.GetterSpecified))
					{
						string ExpectedName = Property.Getter == null ? $"Get{Property.SourceName}" : Property.Getter;
						using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Small))
						{
							StringBuilder Builder = Borrower.StringBuilder;
							Builder.AppendPropertyText(Property, UhtPropertyTextType.GetterSetterArg);
							string ExpectedType = Builder.ToString();
							Property.LogError($"Property '{Property.SourceName}' expected a getter function '[const] {ExpectedType} [&] {ExpectedName}() const', but it was not found");
						}
					}
					if (Property.PropertyExportFlags.HasExactFlags(UhtPropertyExportFlags.SetterSpecified | UhtPropertyExportFlags.SetterSpecifiedNone | UhtPropertyExportFlags.SetterFound, UhtPropertyExportFlags.SetterSpecified))
					{
						string ExpectedName = Property.Setter == null ? $"Set{Property.SourceName}" : Property.Setter;
						using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Small))
						{
							StringBuilder Builder = Borrower.StringBuilder;
							Builder.AppendPropertyText(Property, UhtPropertyTextType.GetterSetterArg);
							string ExpectedType = Builder.ToString();
							Property.LogError($"Property '{Property.SourceName}' expected a setter function 'void {ExpectedName}([const] {ExpectedType} [&] InArg)', but it was not found");
						}
					}
				}
			}
		}
		#endregion

		/// <inheritdoc/>
		public override void CollectReferences(IUhtReferenceCollector Collector)
		{

			// Ignore any intrinsics
			if (this.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
			{
				return;
			}

			// In the original UHT, we don't export IInterface (it wasn't even in the header file)
			if (this.HeaderFile.bIsNoExportTypes && this.SourceName == "IInterface")
			{
				return;
			}

			// Add myself as declarations and cross module references
			Collector.AddDeclaration(this, false);
			Collector.AddDeclaration(this, true);
			Collector.AddCrossModuleReference(this, false);
			Collector.AddCrossModuleReference(this, true);

			// Add the super class
			if (this.SuperClass != null)
			{
				Collector.AddDeclaration(this.SuperClass, true);
				Collector.AddCrossModuleReference(this.SuperClass, true);
			}

			// Add any other base classes
			if (this.Bases != null)
			{
				foreach (UhtStruct Base in this.Bases)
				{
					if (Base is UhtClass BaseClass)
					{
						Collector.AddCrossModuleReference(BaseClass, false);
					}
				}
			}

			// Add the package
			Collector.AddDeclaration(this.Package, true);
			Collector.AddCrossModuleReference(this.Package, true);

			// Collect children references
			foreach (UhtType Child in this.Children)
			{
				Child.CollectReferences(Collector);
			}

			// Done at the end so any contained delegate functions are added first
			// We also add interfaces in the export type order where the native interface
			// is located.  But we add the interface...
			switch (this.ClassType)
			{
				case UhtClassType.NativeInterface:
					if (this.AlternateObject != null && this.AlternateObject is UhtField Field)
					{
						Collector.AddExportType(Field);
					}
					break;
				case UhtClassType.Interface:
					break;
				case UhtClassType.Class:
					Collector.AddExportType(this);
					break;
			}
		}
	}
}
