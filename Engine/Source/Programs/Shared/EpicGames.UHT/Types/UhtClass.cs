// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json.Serialization;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// Series of flags not part of the engine's class flags that affect code generation or verification
	/// </summary>
	[Flags]
	public enum UhtClassExportFlags : int
	{

		/// <summary>
		/// No export flags
		/// </summary>
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

		/// <summary>
		/// Custom constructor specifier present
		/// </summary>
		HasCustomConstructor = 1 << 2,

		/// <summary>
		/// A default constructor was found in the class
		/// </summary>
		HasDefaultConstructor = 1 << 3,

		/// <summary>
		/// An object initializer constructor was found in the class
		/// </summary>
		HasObjectInitializerConstructor = 1 << 4,

		/// <summary>
		/// A custom vtable helper constructor was found
		/// </summary>
		HasCustomVTableHelperConstructor = 1 << 5,

		/// <summary>
		/// A constructor was found
		/// </summary>
		HasConstructor = 1 << 6,

		/// <summary>
		/// GetLifetimeReplicatedProps was found in the class
		/// </summary>
		HasGetLifetimeReplicatedProps = 1 << 7,

		/// <summary>
		/// Class should not be exported
		/// </summary>
		NoExport = 1 << 8,

		/// <summary>
		/// Class has a custom field notify
		/// </summary>
		HasCustomFieldNotify = 1 << 9,

		/// <summary>
		/// Class has a field notify
		/// </summary>
		HasFieldNotify = 1 << 10,
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
		/// <param name="inFlags">Current flags</param>
		/// <param name="testFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtClassExportFlags inFlags, UhtClassExportFlags testFlags)
		{
			return (inFlags & testFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="inFlags">Current flags</param>
		/// <param name="testFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtClassExportFlags inFlags, UhtClassExportFlags testFlags)
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
		public static bool HasExactFlags(this UhtClassExportFlags inFlags, UhtClassExportFlags testFlags, UhtClassExportFlags matchFlags)
		{
			return (inFlags & testFlags) == matchFlags;
		}
	}

	/// <summary>
	/// Type of the class
	/// </summary>
	public enum UhtClassType
	{
		/// <summary>
		/// Class is a UCLASS
		/// </summary>
		Class,

		/// <summary>
		/// Class is a UINTERFACE
		/// </summary>
		Interface,

		/// <summary>
		/// Class is the native interface for a UINTERFACE
		/// </summary>
		NativeInterface,
	}

	/// <summary>
	/// Type of archive serializer found
	/// </summary>
	[Flags]
	public enum UhtSerializerArchiveType
	{

		/// <summary>
		/// No serializer found
		/// </summary>
		None = 0,

		/// <summary>
		/// Archive serializer found
		/// </summary>
		Archive = 1 << 0,

		/// <summary>
		/// Structured archive serializer found
		/// </summary>
		StructuredArchiveRecord = 1 << 1,

		/// <summary>
		/// Mask of all serializer types
		/// </summary>
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
		/// <param name="inFlags">Current flags</param>
		/// <param name="testFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtSerializerArchiveType inFlags, UhtSerializerArchiveType testFlags)
		{
			return (inFlags & testFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="inFlags">Current flags</param>
		/// <param name="testFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtSerializerArchiveType inFlags, UhtSerializerArchiveType testFlags)
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
		public static bool HasExactFlags(this UhtSerializerArchiveType inFlags, UhtSerializerArchiveType testFlags, UhtSerializerArchiveType matchFlags)
		{
			return (inFlags & testFlags) == matchFlags;
		}
	}

	/// <summary>
	/// A skipped declaration
	/// </summary>
	public struct UhtDeclaration
	{

		/// <summary>
		/// Compiler directives when declaration was parsed
		/// </summary>
		public UhtCompilerDirective CompilerDirectives { get; set; }

		/// <summary>
		/// Collection of tokens parsed in the declaration
		/// </summary>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1051:Do not declare visible instance fields", Justification = "<Pending>")]
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE1006:Naming Styles", Justification = "<Pending>")]
		public UhtToken[] Tokens;

		/// <summary>
		/// If this declaration is part of a UFUNCTION, this will be set
		/// </summary>
		public UhtFunction? Function { get; set; }
	}

	/// <summary>
	/// Represents a declaration found by name
	/// </summary>
	public struct UhtFoundDeclaration
	{

		/// <summary>
		/// Compiler directives when declaration was parsed
		/// </summary>
		public UhtCompilerDirective CompilerDirectives { get; set; }

		/// <summary>
		/// Collection of tokens parsed in the declaration
		/// </summary>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1051:Do not declare visible instance fields", Justification = "<Pending>")]
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE1006:Naming Styles", Justification = "<Pending>")]
		public UhtToken[] Tokens;

		/// <summary>
		/// Token index for the matching name
		/// </summary>
		public int NameTokenIndex { get; set; }

		/// <summary>
		/// True if "virtual" was found prior to the matching name
		/// </summary>
		public bool IsVirtual { get; set; }
	}

	/// <summary>
	/// Instance of a UCLASS, UINTERFACE, or a native interface
	/// </summary>
	[UhtEngineClass(Name = "Class")]
	public class UhtClass : UhtStruct
	{
		private List<UhtDeclaration>? _declarations = null;

		/// <summary>
		/// Configuration section
		/// </summary>
		public string Config { get; set; } = String.Empty;

		/// <summary>
		/// If needed, the #if block define for the serializer
		/// </summary>
		public string EnclosingDefine { get; set; } = String.Empty;

		/// <summary>
		/// The class within
		/// </summary>
		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtClass>))]
		public UhtClass ClassWithin { get; set; }

		/// <summary>
		/// Class flags
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EClassFlags ClassFlags { get; set; } = EClassFlags.None;

		/// <summary>
		/// Class cast flags
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EClassCastFlags ClassCastFlags { get; set; } = EClassCastFlags.None;

		/// <summary>
		/// Export flags not present in the engine
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtClassExportFlags ClassExportFlags { get; set; } = UhtClassExportFlags.None;

		/// <summary>
		/// Type of the class
		/// </summary>
		public UhtClassType ClassType { get; set; } = UhtClassType.Class;

		/// <summary>
		/// Type of archivers present
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtSerializerArchiveType SerializerArchiveType { get; set; } = UhtSerializerArchiveType.None;

		/// <summary>
		/// Line number of the prolog
		/// </summary>
		public int PrologLineNumber { get; set; } = -1;

		/// <summary>
		/// Collection of functions and other declarations found in the class
		/// </summary>
		[JsonIgnore]
		public IList<UhtDeclaration>? Declarations => this._declarations;

		/// <summary>
		/// If this, this class is a UINTERFACE and NativeInterface is the associated native interface
		/// </summary>
		public UhtClass? NativeInterface { get; set; } = null;

		/// <summary>
		/// Line number o the generated body statement
		/// </summary>
		public int GeneratedBodyLineNumber { get; set; } = -1;

		/// <summary>
		/// Access of the generated body
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtAccessSpecifier GeneratedBodyAccessSpecifier { get; set; } = UhtAccessSpecifier.None;

		/// <summary>
		/// True if GENERATED_BODY was used.  If false, GENERATED_UCLASS_BODY was used.
		/// </summary>
		public bool HasGeneratedBody { get; set; } = false;

		#region Class specifier parse time values (not interfaces)
		/// <summary>
		/// Engine class flags removed
		/// </summary>
		public EClassFlags RemovedClassFlags { get; set; } = EClassFlags.None;

		/// <summary>
		/// Class within identifier
		/// </summary>
		[JsonIgnore]
		public string ClassWithinIdentifier { get; set; } = String.Empty;

		/// <summary>
		/// Collection of show categories
		/// </summary>
		[JsonIgnore]
		public List<string> ShowCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of hide categories
		/// </summary>
		[JsonIgnore]
		public List<string> HideCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of auto expand categories
		/// </summary>
		[JsonIgnore]
		public List<string> AutoExpandCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of auto collapse categories
		/// </summary>
		[JsonIgnore]
		public List<string> AutoCollapseCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of prioritize categories
		/// </summary>
		[JsonIgnore]
		public List<string> PrioritizeCategories { get; } = new List<string>();

		/// <summary>
		/// Collection of show functions
		/// </summary>
		[JsonIgnore]
		public List<string> ShowFunctions { get; } = new List<string>();

		/// <summary>
		/// Collection of hide functions
		/// </summary>
		[JsonIgnore]
		public List<string> HideFunctions { get; } = new List<string>();

		/// <summary>
		/// Sparse class data types
		/// </summary>
		[JsonIgnore]
		public List<string> SparseClassDataTypes { get; } = new List<string>();

		/// <summary>
		/// Class group names
		/// </summary>
		[JsonIgnore]
		public List<string> ClassGroupNames { get; } = new List<string>();

		/// <summary>
		/// Add the given class flags
		/// </summary>
		/// <param name="flags">Flags to add</param>
		public void AddClassFlags(EClassFlags flags)
		{
			this.ClassFlags |= flags;
			this.RemovedClassFlags &= ~flags;
		}

		/// <summary>
		/// Remove the given class flags
		/// </summary>
		/// <param name="flags">Flags to remove</param>
		public void RemoveClassFlags(EClassFlags flags)
		{
			this.RemovedClassFlags |= flags;
			this.ClassFlags &= ~flags;
		}
		#endregion

		/// <inheritdoc/>
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
		public override string EngineClassName => "Class";

		///<inheritdoc/>
		[JsonIgnore]
		public override bool Deprecated => this.ClassFlags.HasAnyFlags(EClassFlags.Deprecated);

		///<inheritdoc/>
		[JsonIgnore]
		protected override UhtSpecifierValidatorTable? SpecifierValidatorTable
		{
			get
			{
				switch (this.ClassType)
				{
					case UhtClassType.Class:
						return this.Session.GetSpecifierValidatorTable(UhtTableNames.Class);
					case UhtClassType.Interface:
						return this.Session.GetSpecifierValidatorTable(UhtTableNames.Interface);
					case UhtClassType.NativeInterface:
						return this.Session.GetSpecifierValidatorTable(UhtTableNames.NativeInterface);
					default:
						throw new UhtIceException("Unknown class type");
				}
			}
		}

		/// <summary>
		/// The super class
		/// </summary>
		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtClass>))]
		public UhtClass? SuperClass => (UhtClass?)this.Super;

		/// <summary>
		/// Construct a new instance of the class
		/// </summary>
		/// <param name="outer">The outer type</param>
		/// <param name="lineNumber">Line number where class begins</param>
		public UhtClass(UhtType outer, int lineNumber) : base(outer, lineNumber)
		{
			this.ClassWithin = this;
		}

		/// <summary>
		/// True if this class inherits from AActor
		/// </summary>
		[JsonIgnore]
		public bool IsActorClass => IsChildOf(this.Session.AActor);

		///<inheritdoc/>
		[JsonIgnore]
		public override string EngineNamePrefix
		{
			get
			{
				switch (this.ClassType)
				{
					case UhtClassType.Class:
						if (IsActorClass)
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

		/// <summary>
		/// Add the given list of tokens as a possible declaration
		/// </summary>
		/// <param name="compilerDirectives">Currently active compiler directives</param>
		/// <param name="tokens">List of declaration tokens</param>
		/// <param name="function">If parsed as part of a UFUNCTION, this will reference it</param>
		public void AddDeclaration(UhtCompilerDirective compilerDirectives, List<UhtToken> tokens, UhtFunction? function)
		{
			if (this._declarations == null)
			{
				this._declarations = new List<UhtDeclaration>();
			}
			this._declarations.Add(new UhtDeclaration { CompilerDirectives = compilerDirectives, Tokens = tokens.ToArray(), Function = function });
		}

		/// <summary>
		/// Search the declarations for a possible declaration match with the given name
		/// </summary>
		/// <param name="name">Name to be located</param>
		/// <param name="foundDeclaration">Information about the matched declaration</param>
		/// <returns>True if a declaration was found with the name.</returns>
		public bool TryGetDeclaration(string name, out UhtFoundDeclaration foundDeclaration)
		{
			if (name.Length > 0)
			{
				if (this.Declarations != null)
				{
					foreach (UhtDeclaration declaration in this.Declarations)
					{
						bool isVirtual = false;
						for (int index = 0; index < declaration.Tokens.Length; ++index)
						{
							if (declaration.Tokens[index].IsIdentifier("virtual"))
							{
								isVirtual = true;
							}
							else if (declaration.Tokens[index].IsIdentifier(name))
							{
								foundDeclaration = new UhtFoundDeclaration
								{
									CompilerDirectives = declaration.CompilerDirectives,
									Tokens = declaration.Tokens,
									NameTokenIndex = index,
									IsVirtual = isVirtual,
								};
								return true;
							}
						}
					}
				}
			}
			if (this.NativeInterface != null)
			{
				return this.NativeInterface.TryGetDeclaration(name, out foundDeclaration);
			}
			foundDeclaration = new UhtFoundDeclaration();
			return false;
		}

		/// <summary>
		/// Checks to see if the class or any super class has the given flags.
		/// </summary>
		/// <param name="flagsToCheck"></param>
		/// <returns>True if the flags are found</returns>
		public bool HierarchyHasAnyClassFlags(EClassFlags flagsToCheck)
		{
			for (UhtClass? current = this; current != null; current = current.SuperClass)
			{
				if (current.ClassFlags.HasAnyFlags(flagsToCheck))
				{
					return true;
				}
			}
			return false;
		}

		#region Resolution support
		struct GetterSetterToResolve
		{
			public UhtProperty Property { get; set; }
			public bool Setter { get; set; }
			public bool Found { get; set; }
		}

		/// <inheritdoc/>
		protected override void ResolveSuper(UhtResolvePhase resolvePhase)
		{
			base.ResolveSuper(resolvePhase);

			// Make sure the class within is resolved.  We have to make sure we don't try to resolve ourselves.
			if (this.ClassWithin != null && this.ClassWithin != this)
			{
				this.ClassWithin.Resolve(resolvePhase);
			}

			switch (resolvePhase)
			{
				case UhtResolvePhase.Bases:
					BindAndResolveSuper(this.SuperIdentifier, UhtFindOptions.Class);
					BindAndResolveBases(this.BaseIdentifiers, UhtFindOptions.Class);

					// Force the MatchedSerializers on for anything being exported
					if (!this.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.NoExport))
					{
						this.ClassFlags |= EClassFlags.MatchedSerializers;
					}

					switch (this.ClassType)
					{
						case UhtClassType.Class:
							{
								UhtClass? superClass = this.SuperClass;

								// Merge the super class flags
								if (superClass != null)
								{
									this.ClassFlags |= superClass.ClassFlags & EClassFlags.ScriptInherit & ~this.RemovedClassFlags;
								}

								foreach (UhtStruct baseStruct in this.Bases)
								{
									if (baseStruct is UhtClass baseClass)
									{
										if (!baseClass.ClassFlags.HasAnyFlags(EClassFlags.Interface))
										{
											this.LogError($"Class '{baseClass.SourceName}' is not an interface; Can only inherit from non-UObjects or UInterface derived interfaces");
										}
										this.ClassFlags |= baseClass.ClassFlags & EClassFlags.ScriptInherit & ~this.RemovedClassFlags;
									}
								}

								// These following collections only inherit from he parent if they are empty in this class
								if (this.SparseClassDataTypes.Count == 0)
								{
									AppendStringListMetaData(this.SuperClass, UhtNames.SparseClassDataTypes, this.SparseClassDataTypes);
								}
								if (this.PrioritizeCategories.Count == 0)
								{
									AppendStringListMetaData(this.SuperClass, UhtNames.PrioritizeCategories, this.PrioritizeCategories);
								}

								// Merge with categories inherited from the parent.
								MergeCategories();

								SetAndValidateWithinClass(resolvePhase);
								SetAndValidateConfigName();

								// Copy all of the lists back to the meta data
								this.MetaData.AddIfNotEmpty(UhtNames.ClassGroupNames, this.ClassGroupNames);
								this.MetaData.AddIfNotEmpty(UhtNames.AutoCollapseCategories, this.AutoCollapseCategories);
								this.MetaData.AddIfNotEmpty(UhtNames.AutoExpandCategories, this.AutoExpandCategories);
								this.MetaData.AddIfNotEmpty(UhtNames.PrioritizeCategories, this.PrioritizeCategories);
								this.MetaData.AddIfNotEmpty(UhtNames.HideCategories, this.HideCategories);
								this.MetaData.AddIfNotEmpty(UhtNames.ShowCategories, this.ShowCategories);
								this.MetaData.AddIfNotEmpty(UhtNames.SparseClassDataTypes, this.SparseClassDataTypes);
								this.MetaData.AddIfNotEmpty(UhtNames.HideFunctions, this.HideFunctions);

								this.MetaData.Add(UhtNames.IncludePath, this.HeaderFile.IncludeFilePath);
							}
							break;

						case UhtClassType.Interface:
							{
								UhtClass? superClass = this.SuperClass;
								if (superClass != null)
								{
									this.ClassFlags |= superClass.ClassFlags & EClassFlags.ScriptInherit;
									if (!superClass.ClassFlags.HasAnyFlags(EClassFlags.Native))
									{
										throw new UhtException(this, $"Native classes cannot extend non-native classes");
									}
									this.ClassWithin = superClass.ClassWithin;
								}
								else
								{
									this.ClassWithin = this.Session.UObject;
								}
							}
							break;

						case UhtClassType.NativeInterface:
							break;

						default:
							throw new UhtIceException("Unexpected class type");
					}
					break;
			}
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase phase)
		{
			bool result = base.ResolveSelf(phase);

			switch (phase)
			{
				case UhtResolvePhase.InvalidCheck:
					switch (this.ClassType)
					{
						case UhtClassType.Class:
							break;

						case UhtClassType.Interface:
							{
								string nativeInterfaceName = "I" + this.EngineName;
								UhtType? nativeInterface = this.Session.FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, nativeInterfaceName);
								if (nativeInterface == null)
								{
									this.LogError($"UInterface '{this.SourceName}' parsed without a corresponding '{nativeInterfaceName}'");
								}
								else
								{
									// Copy the children
									this.AddChildren(nativeInterface.DetachChildren());
								}
							}
							break;

						case UhtClassType.NativeInterface:
							{
								string interfaceName = "U" + this.EngineName;
								UhtClass? interfaceObj = (UhtClass?)this.Session.FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, interfaceName);
								if (interfaceObj == null)
								{
									if (this.HasGeneratedBody || this.Children.Count != 0)
									{
										this.LogError($"Native interface '{this.SourceName}' parsed without a corresponding '{interfaceName}'");
									}
									else
									{
										this.VisibleType = false;
										result = false;
									}
								}
								else
								{
									this.AlternateObject = interfaceObj;
									interfaceObj.NativeInterface = this;
									//COMPATIBILITY-TODO - Use the native interface access specifier
									interfaceObj.GeneratedBodyAccessSpecifier = this.GeneratedBodyAccessSpecifier;

									if (this.GeneratedBodyLineNumber == -1)
									{
										this.LogError("Expected a GENERATED_BODY() at the start of the native interface");
									}
								}
							}
							break;

						default:
							throw new UhtIceException("Unexpected class type");
					}
					break;

				case UhtResolvePhase.Bases:

					// Check to see if this class matches an entry in the ClassCastFlags.  If it does, that
					// becomes our ClassCastFlags.  If not, then we inherit the flags from the super.
					if (Enum.TryParse<EClassCastFlags>(this.SourceName, false, out EClassCastFlags found))
					{
						if (found != EClassCastFlags.None)
						{
							this.ClassCastFlags = found;
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
						foreach (UhtType child in this.Children)
						{
							if (child is UhtProperty property)
							{
								property.PropertyFlags |= EPropertyFlags.BlueprintReadOnly;
								property.PropertyExportFlags |= UhtPropertyExportFlags.ImpliedBlueprintPure;
							}
						}
					}
					break;

				case UhtResolvePhase.Properties:
					UhtPropertyParser.ResolveChildren(this, UhtPropertyParseOptions.AddModuleRelativePath);
					break;

				case UhtResolvePhase.Final:
					Dictionary<string, List<GetterSetterToResolve>>? gsToResolve = null;
					foreach (UhtType child in this.Children)
					{
						if (child is UhtProperty property)
						{
							if (property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.FieldNotify))
							{
								this.ClassExportFlags |= UhtClassExportFlags.HasFieldNotify;
							}

							if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
							{
								this.ClassExportFlags |= UhtClassExportFlags.SelfHasReplicatedProperties;
								break;
							}

							if (!property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterSpecifiedNone))
							{
								if (property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterSpecified))
								{
									gsToResolve = AddGetterSetter(gsToResolve, property.Getter ?? $"Get{property.SourceName}", property, false);
								}
							}

							if (!property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.SetterSpecifiedNone))
							{
								if (property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.SetterSpecified))
								{
									gsToResolve = AddGetterSetter(gsToResolve, property.Setter ?? $"Set{property.SourceName}", property, true);
								}
							}
						}
						else if (child is UhtFunction function)
						{
							if (function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.FieldNotify))
							{
								this.ClassExportFlags |= UhtClassExportFlags.HasFieldNotify;
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
							if (TryGetDeclaration("GetLifetimeReplicatedProps", out UhtFoundDeclaration _))
							{
								this.ClassExportFlags |= UhtClassExportFlags.HasGetLifetimeReplicatedProps;
							}
						}
					}

					// If we have any getters/setters to resolve 
					if (gsToResolve != null)
					{
						if (this.Declarations != null)
						{
							foreach (UhtDeclaration declaration in this.Declarations)
							{

								// Locate the index of the name
								int nameIndex = 0;
								for (; nameIndex < declaration.Tokens.Length - 1; ++nameIndex)
								{
									if (declaration.Tokens[nameIndex + 1].IsValue('('))
									{
										break;
									}
								}

								string name = declaration.Tokens[nameIndex].Value.ToString();
								if (gsToResolve.TryGetValue(name, out List<GetterSetterToResolve>? gsList))
								{
									for (int index = 0; index < gsList.Count; ++index)
									{
										GetterSetterToResolve gs = gsList[index];
										if (!gs.Found)
										{
											if (TryMatchGetterSetter(gs.Property, gs.Setter, declaration, nameIndex))
											{
												gs.Found = true;
												gsList[index] = gs;
												gs.Property.PropertyExportFlags |= gs.Setter ? UhtPropertyExportFlags.SetterFound : UhtPropertyExportFlags.GetterFound;
												if (gs.Setter)
												{
													gs.Property.Setter = name;
												}
												else
												{
													gs.Property.Getter = name;
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

			return result;
		}

		/// <inheritdoc/>
		protected override void ResolveChildren(UhtResolvePhase phase)
		{
			base.ResolveChildren(phase);

			switch (phase)
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
		public override bool ScanForInstancedReferenced(bool deepScan)
		{
			if (this.ClassFlags.HasAnyFlags(EClassFlags.HasInstancedReference))
			{
				return true;
			}

			if (this.SuperClass != null && this.SuperClass.ScanForInstancedReferenced(deepScan))
			{
				return true;
			}

			return base.ScanForInstancedReferenced(deepScan);
		}

		private void MergeCategories()
		{
			MergeShowCategories();

			// Merge ShowFunctions and HideFunctions
			AppendStringListMetaData(this.SuperClass, UhtNames.HideFunctions, this.HideFunctions);
			foreach (string value in this.ShowFunctions)
			{
				this.HideFunctions.RemoveSwap(value);
			}
			this.ShowFunctions.Clear();

			// Merge AutoExpandCategories and AutoCollapseCategories (we still want to keep AutoExpandCategories though!)
			List<string> parentAutoExpandCategories = GetStringListMetaData(this.SuperClass, UhtNames.AutoExpandCategories);
			List<string> parentAutoCollapseCategories = GetStringListMetaData(this.SuperClass, UhtNames.AutoCollapseCategories);

			foreach (string value in this.AutoExpandCategories)
			{
				this.AutoCollapseCategories.RemoveSwap(value);
				parentAutoCollapseCategories.RemoveSwap(value);
			}

			// Do the same as above but the other way around
			foreach (string value in this.AutoCollapseCategories)
			{
				this.AutoExpandCategories.RemoveSwap(value);
				parentAutoExpandCategories.RemoveSwap(value);
			}

			// Once AutoExpandCategories and AutoCollapseCategories for THIS class have been parsed, add the parent inherited categories
			this.AutoCollapseCategories.AddRange(parentAutoCollapseCategories);
			this.AutoExpandCategories.AddRange(parentAutoExpandCategories);
		}

		private void MergeShowCategories()
		{

			// Add the super class hide categories and prime the output show categories with the parent
			List<string> outShowCategories = GetStringListMetaData(this.SuperClass, UhtNames.ShowCategories);
			AppendStringListMetaData(this.SuperClass, UhtNames.HideCategories, this.HideCategories);

			// If this class has new show categories, then merge them
			if (this.ShowCategories.Count != 0)
			{
				StringBuilder subCategoryPath = new();
				foreach (string value in this.ShowCategories)
				{

					// if we didn't find this specific category path in the HideCategories metadata
					if (!this.HideCategories.RemoveSwap(value))
					{
						string[] subCategories = value.ToString().Split('|', StringSplitOptions.RemoveEmptyEntries);

						subCategoryPath.Clear();
						// look to see if any of the parent paths are excluded in the HideCategories list
						for (int categoryPathIndex = 0; categoryPathIndex < subCategories.Length - 1; ++categoryPathIndex)
						{
							subCategoryPath.Append(subCategories[categoryPathIndex]);
							// if we're hiding a parent category, then we need to flag this sub category for show
							if (HideCategories.Contains(subCategoryPath.ToString()))
							{
								outShowCategories.AddUnique(value);
								break;
							}
							subCategoryPath.Append('|');
						}
					}
				}
			}

			// Replace the show categories
			this.ShowCategories.Clear();
			this.ShowCategories.AddRange(outShowCategories);
		}

		private void SetAndValidateWithinClass(UhtResolvePhase resolvePhase)
		{
			// The class within must be a child of any super class within
			UhtClass expectedClassWithin = this.SuperClass != null ? this.SuperClass.ClassWithin : this.Session.UObject;

			// Process all of the class specifiers
			if (!String.IsNullOrEmpty(this.ClassWithinIdentifier))
			{
				UhtClass? specifiedClassWithin = (UhtClass?)this.Session.FindType(null, UhtFindOptions.EngineName | UhtFindOptions.Class, this.ClassWithinIdentifier);
				if (specifiedClassWithin == null)
				{
					this.LogError($"Within class '{this.ClassWithinIdentifier}' not found");
				}
				else
				{

					// Make sure the class within has been resolved to this phase.  We don't need to worry about the super since we know 
					// that it has already been resolved.
					if (specifiedClassWithin != this)
					{
						specifiedClassWithin.Resolve(resolvePhase);
					}

					if (specifiedClassWithin.IsChildOf(this.Session.UInterface))
					{
						this.LogError("Classes cannot be 'within' interfaces");
					}
					else if (!specifiedClassWithin.IsChildOf(expectedClassWithin))
					{
						this.LogError($"Cannot override within class with '{specifiedClassWithin.SourceName}' since it isn't a child of parent class's expected within '{expectedClassWithin.SourceName}'");
					}
					else
					{
						this.ClassWithin = specifiedClassWithin;
					}
				}
			}

			// If we don't have a class within set, then just use the expected within
			else
			{
				this.ClassWithin = expectedClassWithin;
			}
		}

		private void SetAndValidateConfigName()
		{
			// Since this flag is computed in this method, we have to re-propagate the flag from the super
			// just in case they were defined in this source file.
			if (this.SuperClass != null)
			{
				this.ClassFlags |= this.SuperClass.ClassFlags & EClassFlags.Config;
			}

			// Set the class config flag if any properties have config
			if (!this.ClassFlags.HasAnyFlags(EClassFlags.Config))
			{
				foreach (UhtProperty property in this.Properties)
				{
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.Config))
					{
						this.ClassFlags |= EClassFlags.Config;
						break;
					}
				}
			}

			if (this.Config.Length > 0)
			{
				// if the user specified "inherit", we're just going to use the parent class's config filename
				// this is not actually necessary but it can be useful for explicitly communicating config-ness
				if (this.Config.Equals("inherit", StringComparison.OrdinalIgnoreCase))
				{
					if (this.SuperClass == null)
					{
						this.LogError($"Cannot inherit config filename for class '{this.SourceName}' when it has no super class");
					}
					else if (this.SuperClass.Config.Length == 0)
					{
						this.LogError($"Cannot inherit config filename for class '{this.SourceName}' when parent class '{this.SuperClass.SourceName}' has no config filename");
					}
					else
					{
						this.Config = this.SuperClass.Config;
					}
				}
			}
			else if (this.ClassFlags.HasAnyFlags(EClassFlags.Config) && this.SuperClass != null)
			{
				this.Config = this.SuperClass.Config;
			}

			if (this.ClassFlags.HasAnyFlags(EClassFlags.Config) && this.Config.Length == 0)
			{
				this.LogError("Classes with config / globalconfig member variables need to specify config file.");
				this.Config = "Engine";
			}
		}

		private static List<string> GetStringListMetaData(UhtType? type, string key)
		{
			List<string> outStrings = new();
			AppendStringListMetaData(type, key, outStrings);
			return outStrings;
		}

		private static void AppendStringListMetaData(UhtType? type, string key, List<string> stringList)
		{
			if (type != null)
			{
				string[]? values = type.MetaData.GetStringArray(key);
				if (values != null)
				{
					foreach (string value in values)
					{
						//COMPATIBILITY-TODO - TEST - This preserves duplicates that are in old UHT
						//StringList.AddUnique(Value);
						stringList.Add(value);
					}
				}
			}
		}

		/// <summary>
		/// Given a token stream, verify that it matches the expected signature for a getter/setter
		/// </summary>
		/// <param name="property">Property requesting the setter/getter</param>
		/// <param name="setter">If true, a setter is expected</param>
		/// <param name="declaration">The declaration being tested</param>
		/// <param name="nameIndex">The index of the token with the expected getter/setter name</param>
		/// <returns>True if the declaration matches</returns>
		private static bool TryMatchGetterSetter(UhtProperty property, bool setter, UhtDeclaration declaration, int nameIndex)
		{
			UhtToken[] tokens = declaration.Tokens;
			int tokenIndex = 0;
			int tokenCount = tokens.Length;

			// Skip all of the API and virtual keywords
			for (; tokenIndex < tokenCount && (tokens[tokenIndex].IsValue("virtual") || tokens[tokenIndex].Value.Span.EndsWith("_API")); ++tokenIndex)
			{
				// Nothing to do in the body
			}

			int typeIndex;
			int typeCount;

			// Verify the format of the function declaration and extract the type
			if (setter)
			{

				// The return type must be 'void'
				if (tokenIndex == tokenCount || !tokens[tokenIndex].IsValue("void"))
				{
					return false;
				}
				tokenIndex++;

				// Followed by the name
				if (tokenIndex == tokenCount || tokenIndex != nameIndex)
				{
					return false;
				}
				tokenIndex++;

				// Followed by the '('
				if (tokenIndex == tokenCount || !tokens[tokenIndex].IsValue('('))
				{
					return false;
				}
				tokenIndex++;

				// Skip any UPARAM only in actual functions
				if (declaration.Function != null && tokenIndex < tokenCount && tokens[tokenIndex].IsIdentifier("UPARAM"))
				{
					tokenIndex++;
					if (tokenIndex == tokenCount || !tokens[tokenIndex].IsValue('('))
					{
						return false;
					}
					tokenIndex++;
					int paramCount = 1;
					while (tokenIndex < tokenCount && paramCount > 0)
					{
						if (tokens[tokenIndex].IsValue(')'))
						{
							paramCount--;
						}
						else if (tokens[tokenIndex].IsValue('('))
						{
							paramCount++;
						}
						tokenIndex++;
					}
					if (paramCount > 0)
					{
						return false;
					}
				}

				// Find the ')'
				typeIndex = tokenIndex;
				while (true)
				{
					if (tokenIndex == tokenCount)
					{
						return false;
					}
					if (tokens[tokenIndex].IsValue(')'))
					{
						typeCount = tokenIndex - typeIndex;
						break;
					}
					tokenIndex++;
				}
			}
			else
			{
				// Get the type range
				typeIndex = tokenIndex;
				typeCount = nameIndex - typeIndex;
				tokenIndex = nameIndex + 1;

				// Followed by the '('
				if (tokenIndex == tokenCount || !tokens[tokenIndex].IsValue('('))
				{
					return false;
				}
				tokenIndex++;

				// Followed by the ')'
				if (tokenIndex == tokenCount || !tokens[tokenIndex].IsValue(')'))
				{
					return false;
				}
				tokenIndex++;

				// Followed by the "const"
				if (tokenIndex == tokenCount || !tokens[tokenIndex].IsValue("const"))
				{
					return false;
				}
#pragma warning disable IDE0059 // Unnecessary assignment of a value
				tokenIndex++;
#pragma warning restore IDE0059 // Unnecessary assignment of a value
			}

			// Verify the type
			{
				using BorrowStringBuilder borrower = new(StringBuilderCache.Small);
				StringBuilder builder = borrower.StringBuilder;
				builder.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg);
				if (property.IsStaticArray)
				{
					if (declaration.Function == null)
					{
						builder.Append('*');
					}
					else
					{
						builder.Append('[').Append(property.ArrayDimensions).Append(']');
					}
				}
				string expectedType = builder.ToString();
				int typeEnd = typeIndex + typeCount;

				// We will do a somewhat brute force string compare to avoid constructing an expected
				// string or parsing the required string
				ReadOnlySpan<char> expectedSpan = expectedType.AsSpan().Trim();

				// skip const. We allow const in parameter and return values even if the property is not const
				if (typeIndex < typeEnd && tokens[typeIndex].IsIdentifier("const"))
				{
					typeIndex++;
				}

				// Loop until we are done
				while (typeIndex < typeEnd && expectedSpan.Length > 0)
				{
					UhtToken token = tokens[typeIndex];
					ReadOnlySpan<char> tokenSpan = token.Value.Span;

					// Make sure we match the expected type
					if (tokenSpan.Length > expectedSpan.Length ||
						!expectedSpan.StartsWith(tokenSpan))
					{
						return false;
					}

					// Extract the remaining part of the string we expect to match
					expectedSpan = expectedSpan[tokenSpan.Length..].TrimStart();
					typeIndex++;
				}

				// Consume any '&' for reference support
				if (typeIndex < typeEnd && tokens[typeIndex].IsSymbol('&'))
				{
					typeIndex++;
				}

				// For setters, there can be an identifier
				if (setter && typeIndex < typeEnd && tokens[typeIndex].IsIdentifier())
				{
					typeIndex++;
				}

				// We must be at the end for a match
				if (typeIndex != typeEnd)
				{
					return false;
				}
			}

			// Validate the #if blocks.  This should really be done during validation
			string funcType = setter ? "setter" : "getter";
			UhtPropertyExportFlags testFlag = setter ? UhtPropertyExportFlags.SetterSpecified : UhtPropertyExportFlags.GetterSpecified;
			if (declaration.CompilerDirectives.HasAnyFlags(UhtCompilerDirective.WithEditor))
			{
				if (property.PropertyExportFlags.HasAnyFlags(testFlag))
				{
					property.LogError(tokens[0].InputLine, $"Property {property.SourceName} {funcType} function {tokens[nameIndex].Value} "
						+ "cannot be declared within WITH_EDITOR block. Use WITH_EDITORONLY_DATA instead.");
				}
				return false;
			}
			if (declaration.CompilerDirectives.HasAnyFlags(UhtCompilerDirective.WithEditorOnlyData) && !property.PropertyFlags.HasAnyFlags(EPropertyFlags.EditorOnly))
			{
				if (property.PropertyExportFlags.HasAnyFlags(testFlag))
				{
					property.LogError(tokens[0].InputLine, $"Property {property.SourceName} is not editor-only but its {funcType} function '{tokens[nameIndex].Value}' is");
				}
				return false;
			}
			if (declaration.Function != null)
			{
				if (!declaration.Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
				{
					property.LogError(tokens[0].InputLine, $"Property {property.SourceName} {funcType} function '{tokens[nameIndex].Value}' has to be native");
				}
				if (declaration.Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net | EFunctionFlags.Event))
				{
					property.LogError(tokens[0].InputLine, $"Property {property.SourceName} {funcType} function '{tokens[nameIndex].Value}' cannot be a net or an event");
				}
			}
			return true;
		}

		/// <summary>
		/// Add a requested getter/setter function name
		/// </summary>
		/// <param name="gsToResolve">Dictionary containing the lookup by function name.  Will be created if null.</param>
		/// <param name="name">Name of the getter/setter</param>
		/// <param name="property">Property requesting the getter/setter</param>
		/// <param name="setter">True if this is a setter</param>
		/// <returns>Resulting dictionary</returns>
		private static Dictionary<string, List<GetterSetterToResolve>> AddGetterSetter(Dictionary<string, List<GetterSetterToResolve>>? gsToResolve, string name, UhtProperty property, bool setter)
		{
			if (gsToResolve == null)
			{
				gsToResolve = new Dictionary<string, List<GetterSetterToResolve>>();
			}
			if (!gsToResolve.TryGetValue(name, out List<GetterSetterToResolve>? gsList))
			{
				gsList = new List<GetterSetterToResolve>();
				gsToResolve.Add(name, gsList);
			}
			gsList.Add(new GetterSetterToResolve { Property = property, Setter = setter });
			return gsToResolve;
		}
		#endregion

		#region Validation support
		/// <inheritdoc/>
		protected override UhtValidationOptions Validate(UhtValidationOptions options)
		{
			options = base.Validate(options);

			// Classes must start with a valid prefix
			string expectedClassName = this.EngineNamePrefix + this.EngineName;
			if (expectedClassName != this.SourceName)
			{
				this.LogError($"Class '{this.SourceName}' has an invalid Unreal prefix, expecting '{expectedClassName}'");
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

			if (this.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.HasFieldNotify))
			{
				if (this.Session.INotifyFieldValueChanged == null)
				{
					this.LogError($"UClass '{this.SourceName}' has FieldNotify elements but the interface 'INotifyFieldValueChanged' is not defined.");
				}
				else if (!ImplementsInterface(this.Session.INotifyFieldValueChanged))
				{
					this.LogError($"UClass '{this.SourceName}' need to implement the interface INotifyFieldValueChanged' to support FieldNotify.");
				}
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
						// Iterate over all the bases looking for any interfaces
						foreach (UhtStruct baseStruct in this.Bases)
						{

							// Skip children that aren't interfaces or if a common ancestor
							UhtClass? baseClass = baseStruct as UhtClass;
							if (baseClass == null || baseClass.ClassType == UhtClassType.Class || IsChildOf(baseClass))
							{
								continue;
							}

							// Loop through the function to make sure they are implemented
							List<UhtType> baseChildren = baseClass.AlternateObject != null ? baseClass.AlternateObject.Children : baseClass.Children;
							foreach (UhtType baseChild in baseChildren)
							{
								UhtFunction? baseFunction = baseChild as UhtFunction;
								if (baseFunction == null)
								{
									continue;
								}

								// Delegate signature functions are simple stubs and aren't required to be implemented (they are not callable)
								bool implemented = baseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate);

								// Try to find an existing function
								foreach (UhtType child in this.Children)
								{
									UhtFunction? function = child as UhtFunction;
									if (function == null || function.SourceName != baseFunction.SourceName)
									{
										continue;
									}

									if (baseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Event) && !function.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
									{
										function.LogError($"Implementation of function '{function.SourceName}' must be declared as 'event' to match declaration in interface '{baseClass.SourceName}'");
									}

									if (baseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate) && !function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
									{
										function.LogError($"Implementation of function '{function.SourceName}' must be declared as 'delegate' to match declaration in interface '{baseClass.SourceName}'");
									}

									implemented = true;

									if (baseFunction.Children.Count != function.Children.Count)
									{
										function.LogError($"Implementation of function '{function.SourceName}' conflicts with interface '{baseClass.SourceName}' - different number of parameters ({function.Children.Count}/{baseFunction.Children.Count})");
										continue;
									}

									for (int index = 0; index < function.Children.Count; ++index)
									{
										UhtProperty baseProperty = (UhtProperty)baseFunction.Children[index];
										UhtProperty property = (UhtProperty)function.Children[index];
										if (!baseProperty.MatchesType(property))
										{
											if (baseProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm))
											{
												function.LogError($"Implementation of function '{function.SourceName}' conflicts only by return type with interface '{baseClass.SourceName}'");
											}
											else
											{
												function.LogError($"Implementation of function '{function.SourceName}' conflicts type with interface '{baseClass.SourceName}' - parameter {index} '{property.SourceName}'");
											}
										}
									}
								}

								// Verify that if this has blueprint-callable functions that are not implementable events, we've implemented them as a UFunction in the target class
								if (!implemented
									&& baseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable)
									&& !baseFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent)
									&& !baseClass.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint)
									&& (baseClass.AlternateObject == null || !baseClass.AlternateObject.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint)))
								{
									this.LogError($"Missing UFunction implementation of function '{baseFunction.SourceName}' from interface '{baseClass.SourceName}'.  This function needs a UFUNCTION() declaration.");
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
						bool canImplementInBlueprints = this.MetaData.GetBoolean(UhtNames.IsBlueprintBase);
						bool cannotImplementInBlueprints = (!canImplementInBlueprints && this.MetaData.ContainsKey(UhtNames.IsBlueprintBase))
							|| this.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint);

						//bool canImplementInterfaceInBlueprint = !this.MetaData.ContainsKey(UhtNames.CannotImplementInterfaceInBlueprint);
						foreach (UhtType child in this.Children)
						{
							if (child is UhtFunction function)
							{
								// Verify interfaces with respect to their blueprint accessible functions
								if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent) && !function.MetaData.GetBoolean(UhtNames.BlueprintInternalUseOnly))
								{
									// Ensure that blueprint events are only allowed in implementable interfaces. Internal only functions allowed
									if (cannotImplementInBlueprints)
									{
										function.LogError("Interfaces that are not implementable in blueprints cannot have Blueprint Event members.");
									}
									if (!canImplementInBlueprints)
									{
										// We do not currently warn about this case as there are a large number of existing interfaces that do not specify
										// Function.LogWarning(TEXT("Interfaces with Blueprint Events should declare Blueprintable on the interface."));
									}
								}

								if (function.FunctionFlags.HasExactFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.BlueprintEvent, EFunctionFlags.BlueprintCallable))
								{
									// Ensure that if this interface contains blueprint callable functions that are not blueprint defined, that it must be implemented natively
									if (canImplementInBlueprints)
									{
										function.LogError("Blueprint implementable interfaces cannot contain BlueprintCallable functions that are not "
											+ "BlueprintImplementableEvents. Add NotBlueprintable to the interface if you wish to keep this function.");
									}
									if (!cannotImplementInBlueprints)
									{
										// Lowered this case to a warning instead of error, they will not show up as blueprintable unless they also have events
										function.LogWarning("Interfaces with BlueprintCallable functions but no events should explicitly declare NotBlueprintable on the interface.");
									}
								}
							}
						}
					}
					break;

				case UhtClassType.NativeInterface:
					break;
			}

			return options |= UhtValidationOptions.Shadowing | UhtValidationOptions.Deprecated;
		}

		static void ValidateBlueprintPopertyGetter(UhtProperty property, string functionName, UhtFunction? targetFunction)
		{
			if (targetFunction == null)
			{
				property.LogError($"Blueprint Property getter function '{functionName}' not found");
				return;
			}

			if (targetFunction.ParameterProperties.Length != 0)
			{
				targetFunction.LogError($"Blueprint Property getter function '{targetFunction.SourceName}' must not have parameters.");
			}

			UhtProperty? returnProperty = targetFunction.ReturnProperty;
			if (returnProperty == null || !property.IsSameType(returnProperty))
			{
				targetFunction.LogError($"Blueprint Property getter function '{targetFunction.SourceName}' must have return value of type '{property.GetUserFacingDecl()}'.");
			}

			if (targetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
			{
				targetFunction.LogError($"Blueprint Property getter function '{targetFunction.SourceName}' cannot be a blueprint event.");
			}

			if (!targetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure))
			{
				targetFunction.LogError($"Blueprint Property getter function '{targetFunction.SourceName}' must be pure.");
			}
		}

		static void ValidateBlueprintPopertySetter(UhtProperty property, string functionName, UhtFunction? targetFunction)
		{
			if (targetFunction == null)
			{
				property.LogError($"Blueprint Property setter function '{functionName}' not found");
				return;
			}

			if (targetFunction.ReturnProperty != null)
			{
				targetFunction.LogError($"Blueprint Property setter function '{targetFunction.SourceName}' must not have a return value.");
			}

			if (targetFunction.ParameterProperties.Length != 1 || !property.IsSameType((UhtProperty)targetFunction.ParameterProperties.Span[0]))
			{
				targetFunction.LogError($"Blueprint Property setter function '{targetFunction.SourceName}' must have exactly one parameter of type '{property.GetUserFacingDecl()}'.");
			}

			if (targetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
			{
				targetFunction.LogError($"Blueprint Property setter function '{targetFunction.SourceName}' cannot be a blueprint event.");
			}

			if (!targetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable))
			{
				targetFunction.LogError($"Blueprint Property setter function '{targetFunction.SourceName}' must be a blueprint callable.");
			}

			if (targetFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure))
			{
				targetFunction.LogError($"Blueprint Property setter function '{targetFunction.SourceName}' must not be pure.");
			}
		}

		static void ValidateRepNotifyCallback(UhtProperty property, string functionName, UhtFunction? targetFunction)
		{
			if (targetFunction == null)
			{
				property.LogError($"Replication notification function '{functionName}' not found");
				return;
			}

			if (targetFunction.ReturnProperty != null)
			{
				targetFunction.LogError($"Replication notification function '{targetFunction.SourceName}' must not have a return value.");
			}

			bool isArrayProperty = property.IsStaticArray || property is UhtArrayProperty;
			int maxParms = isArrayProperty ? 2 : 1;
			ReadOnlyMemory<UhtType> parameters = targetFunction.ParameterProperties;

			if (parameters.Length > maxParms)
			{
				targetFunction.LogError($"Replication notification function '{targetFunction.SourceName}' has too many parameters.");
			}

			if (parameters.Length >= 1)
			{
				UhtProperty parm = (UhtProperty)parameters.Span[0];
				// First parameter is always the old value:
				if (!property.IsSameType(parm))
				{
					targetFunction.LogError($"Replication notification function '{targetFunction.SourceName}' first (optional) parameter must be of type '{property.GetUserFacingDecl()}'.");
				}
			}

			if (parameters.Length >= 2)
			{
				UhtProperty parm = (UhtProperty)parameters.Span[1];
				// First parameter is always the old value:
				bool isValid = false;
				if (parm is UhtArrayProperty arrayProperty)
				{
					isValid = arrayProperty.ValueProperty is UhtByteProperty && parm.PropertyFlags.HasAllFlags(EPropertyFlags.ConstParm | EPropertyFlags.ReferenceParm);
				}
				if (!isValid)
				{
					targetFunction.LogError($"Replication notification function '{targetFunction.SourceName}' second (optional) parameter must be of type 'const TArray<uint8>&'.");
				}
			}
		}

		void ValidateProperties()
		{
			foreach (UhtType child in this.Children)
			{
				if (child is UhtProperty property)
				{
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.RepNotify))
					{
						string name = property.RepNotifyName ?? String.Empty;
						ValidateRepNotifyCallback(property, name, (UhtFunction?)FindType(UhtFindOptions.EngineName | UhtFindOptions.Function, name.ToString()));
					}

					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintVisible))
					{
						if (property.MetaData.TryGetValue(UhtNames.BlueprintGetter, out string? getter) && getter.Length > 0)
						{
							ValidateBlueprintPopertyGetter(property, getter, (UhtFunction?)FindType(UhtFindOptions.EngineName | UhtFindOptions.Function, getter));
						}

						if (!property.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly))
						{
							if (property.MetaData.TryGetValue(UhtNames.BlueprintSetter, out string? setter) && setter.Length > 0)
							{
								ValidateBlueprintPopertySetter(property, setter, (UhtFunction?)FindType(UhtFindOptions.EngineName | UhtFindOptions.Function, setter));
							}
						}
					}

					// Validate if we are using editor only data in a class or struct definition
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.EditorOnly) && this.ClassFlags.HasAnyFlags(EClassFlags.Optional))
					{
						property.LogError("Cannot specify an editor only property inside an optional class.");
					}

					// Check for getter/setter
					if (property.PropertyExportFlags.HasExactFlags(UhtPropertyExportFlags.GetterSpecified | UhtPropertyExportFlags.GetterSpecifiedNone | 
						UhtPropertyExportFlags.GetterFound, UhtPropertyExportFlags.GetterSpecified))
					{
						using BorrowStringBuilder borrower = new(StringBuilderCache.Small);
						StringBuilder builder = borrower.StringBuilder;
						string expectedName = property.Getter ?? $"Get{property.SourceName}";
						builder.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg);
						if (property.IsStaticArray)
						{
							builder.Append($"*/[{property.ArrayDimensions}]");
						}
						string expectedType = builder.ToString();
						property.LogError($"Property '{property.SourceName}' expected a getter function '[const] {expectedType} [&] {expectedName}() const', but it was not found");
					}
					if (property.PropertyExportFlags.HasExactFlags(UhtPropertyExportFlags.SetterSpecified | UhtPropertyExportFlags.SetterSpecifiedNone | UhtPropertyExportFlags.SetterFound, UhtPropertyExportFlags.SetterSpecified))
					{
						using BorrowStringBuilder borrower = new(StringBuilderCache.Small);
						StringBuilder builder = borrower.StringBuilder;
						string expectedName = property.Setter ?? $"Set{property.SourceName}";
						builder.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg);
						if (property.IsStaticArray)
						{
							builder.Append($"*/[{property.ArrayDimensions}]");
						}
						string expectedType = builder.ToString();
						property.LogError($"Property '{property.SourceName}' expected a setter function 'void {expectedName}([const] {expectedType} [&] InArg)', but it was not found");
					}
				}
			}
		}
		#endregion

		/// <inheritdoc/>
		public override void CollectReferences(IUhtReferenceCollector collector)
		{

			// Ignore any intrinsics
			if (this.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
			{
				return;
			}

			// In the original UHT, we don't export IInterface (it wasn't even in the header file)
			if (this.HeaderFile.IsNoExportTypes && this.SourceName == "IInterface")
			{
				return;
			}

			// Add myself as declarations and cross module references
			collector.AddDeclaration(this, false);
			collector.AddDeclaration(this, true);
			collector.AddCrossModuleReference(this, false);
			collector.AddCrossModuleReference(this, true);

			// Add the super class
			if (this.SuperClass != null)
			{
				collector.AddDeclaration(this.SuperClass, true);
				collector.AddCrossModuleReference(this.SuperClass, true);
			}

			// Add any other base classes
			foreach (UhtStruct @base in this.Bases)
			{
				if (@base is UhtClass baseClass)
				{
					collector.AddCrossModuleReference(baseClass, false);
				}
			}

			// Add the package
			collector.AddDeclaration(this.Package, true);
			collector.AddCrossModuleReference(this.Package, true);

			// Collect children references
			foreach (UhtType child in this.Children)
			{
				child.CollectReferences(collector);
			}

			// Done at the end so any contained delegate functions are added first
			// We also add interfaces in the export type order where the native interface
			// is located.  But we add the interface...
			switch (this.ClassType)
			{
				case UhtClassType.NativeInterface:
					if (this.AlternateObject != null && this.AlternateObject is UhtField field)
					{
						collector.AddExportType(field);
					}
					break;
				case UhtClassType.Interface:
					break;
				case UhtClassType.Class:
					collector.AddExportType(this);
					break;
			}
		}

		private bool ImplementsInterface(UhtClass interfaceObj)
		{
			for (UhtClass? superClass = this; superClass != null; superClass = superClass.SuperClass)
			{
				foreach (UhtStruct structObj in superClass.Bases)
				{
					if (structObj.IsChildOf(interfaceObj))
					{
						return true;
					}
				}
			}
			return false;
		}
	}
}
