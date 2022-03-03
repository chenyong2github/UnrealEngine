// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json.Serialization;
using System.Threading;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Enumeration for all the different engine types
	/// </summary>
	public enum UhtEngineType
	{

		/// <summary>
		/// Type is a header file (UhtHeader)
		/// </summary>
		Header,

		/// <summary>
		/// Type is a package (UhtPackage)
		/// </summary>
		Package,

		/// <summary>
		/// Type is a class (UhtClass)
		/// </summary>
		Class,

		/// <summary>
		/// Type is an interface (UhtClass)
		/// </summary>
		Interface,

		/// <summary>
		/// Type is a native interface (UhtClass)
		/// </summary>
		NativeInterface,

		/// <summary>
		/// Type is a script struct (UhtScriptStruct)
		/// </summary>
		ScriptStruct,

		/// <summary>
		/// Type is an enumeration (UhtEnum)
		/// </summary>
		Enum,

		/// <summary>
		/// Type is a function (UhtFunction)
		/// </summary>
		Function,

		/// <summary>
		/// Type is a delegate (UhtFunction)
		/// </summary>
		Delegate,

		/// <summary>
		/// Type is a property (UhtProperty)
		/// </summary>
		Property,
	}

	/// <summary>
	/// Collection of extension methods used to query information about the type
	/// </summary>
	public static class UhtEngineTypeExtensions
	{

		/// <summary>
		/// Get the name of the type where shared distinctions aren't made between different class and function types.
		/// </summary>
		/// <param name="EngineType">The engine type in question</param>
		/// <returns>Lowercase name</returns>
		/// <exception cref="UhtIceException">If the requested type isn't supported</exception>
		public static string ShortLowercaseText(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Header: return "header";
				case UhtEngineType.Package: return "package";
				case UhtEngineType.Class: 
				case UhtEngineType.Interface:
				case UhtEngineType.NativeInterface: return "class";
				case UhtEngineType.ScriptStruct: return "struct";
				case UhtEngineType.Enum: return "enum";
				case UhtEngineType.Function:
				case UhtEngineType.Delegate: return "function";
				case UhtEngineType.Property: return "property";
				default:
					throw new UhtIceException("Invalid engine extended type");
			}
		}

		/// <summary>
		/// Get the name of the type where shared distinctions aren't made between different class and function types.
		/// </summary>
		/// <param name="EngineType">The engine type in question</param>
		/// <returns>Capitalized name</returns>
		/// <exception cref="UhtIceException">If the requested type isn't supported</exception>
		public static string ShortCapitalizedText(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Header: return "Header";
				case UhtEngineType.Package: return "Package";
				case UhtEngineType.Class:
				case UhtEngineType.Interface:
				case UhtEngineType.NativeInterface: return "Class";
				case UhtEngineType.ScriptStruct: return "Struct";
				case UhtEngineType.Enum: return "Enum";
				case UhtEngineType.Function:
				case UhtEngineType.Delegate: return "Function";
				case UhtEngineType.Property: return "Property";
				default:
					throw new UhtIceException("Invalid engine extended type");
			}
		}

		/// <summary>
		/// Get the name of the type
		/// </summary>
		/// <param name="EngineType">The engine type in question</param>
		/// <returns>Lowercase name</returns>
		/// <exception cref="UhtIceException">If the requested type isn't supported</exception>
		public static string LowercaseText(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Header: return "header";
				case UhtEngineType.Package: return "package";
				case UhtEngineType.Class: return "class";
				case UhtEngineType.Interface: return "interface";
				case UhtEngineType.NativeInterface: return "native interface";
				case UhtEngineType.ScriptStruct: return "struct";
				case UhtEngineType.Enum: return "enum";
				case UhtEngineType.Function: return "function";
				case UhtEngineType.Delegate: return "delegate";
				case UhtEngineType.Property: return "property";
				default:
					throw new UhtIceException("Invalid engine extended type");
			}
		}


		/// <summary>
		/// Get the name of the type
		/// </summary>
		/// <param name="EngineType">The engine type in question</param>
		/// <returns>Capitalized name</returns>
		/// <exception cref="UhtIceException">If the requested type isn't supported</exception>
		public static string CapitalizedText(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Header: return "Header";
				case UhtEngineType.Package: return "Package";
				case UhtEngineType.Class: return "Class";
				case UhtEngineType.Interface: return "Interface";
				case UhtEngineType.NativeInterface: return "Native interface";
				case UhtEngineType.ScriptStruct: return "Struct";
				case UhtEngineType.Enum: return "Enum";
				case UhtEngineType.Function: return "Function";
				case UhtEngineType.Delegate: return "Delegate";
				case UhtEngineType.Property: return "Property";
				default:
					throw new UhtIceException("Invalid engine extended type");
			}
		}

		/// <summary>
		/// Return true if the type must be unique in the symbol table
		/// </summary>
		/// <param name="EngineType">Type in question</param>
		/// <returns>True if it must be unique</returns>
		public static bool MustBeUnique(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Class:
				case UhtEngineType.Interface:
				case UhtEngineType.NativeInterface:
				case UhtEngineType.ScriptStruct:
				case UhtEngineType.Enum:
					return true;

				default:
					return false;
			}
		}

		/// <summary>
		/// Find options to be used when attempting to find a duplicate name
		/// </summary>
		/// <param name="EngineType">Type in question</param>
		/// <returns>Find options to be used for search</returns>
		public static UhtFindOptions MustBeUniqueFindOptions(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Class:
				case UhtEngineType.Interface:
				case UhtEngineType.ScriptStruct:
				case UhtEngineType.Enum:
					return UhtFindOptions.Class | UhtFindOptions.ScriptStruct | UhtFindOptions.Enum;

				case UhtEngineType.NativeInterface:
					return UhtFindOptions.Class;

				default:
					return (UhtFindOptions)0;
			}
		}

		/// <summary>
		/// Return true if the type must not be a reserved name.
		/// </summary>
		/// <param name="EngineType">Type in question</param>
		/// <returns>True if the type must not have a reserved name.</returns>
		public static bool MustNotBeReserved(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Class:
				case UhtEngineType.Interface:
				case UhtEngineType.NativeInterface:
				case UhtEngineType.ScriptStruct:
				case UhtEngineType.Enum:
					return true;

				default:
					return false;
			}
		}

		/// <summary>
		/// Return true if the type should be added to the engine symbol table
		/// </summary>
		/// <param name="EngineType">Type in question</param>
		/// <returns>True if the type should be added to the symbol table</returns>
		public static bool HasEngineName(this UhtEngineType EngineType)
		{
			return EngineType != UhtEngineType.NativeInterface;
		}

		/// <summary>
		/// Return true if the children of the type should be added to the symbol table
		/// </summary>
		/// <param name="EngineType">Type in question</param>
		/// <returns>True if children should be added</returns>
		public static bool AddChildrenToSymbolTable(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Property:
					return false;

				default:
					return true;
			}
		}

		/// <summary>
		/// Convert the engine type to find options
		/// </summary>
		/// <param name="EngineType">Engine type</param>
		/// <returns>Find options</returns>
		/// <exception cref="UhtIceException">Engine type is invalid</exception>
		public static UhtFindOptions FindOptions(this UhtEngineType EngineType)
		{
			switch (EngineType)
			{
				case UhtEngineType.Header: return 0;
				case UhtEngineType.Package: return 0;
				case UhtEngineType.Class: return UhtFindOptions.Class;
				case UhtEngineType.Interface: return UhtFindOptions.Class;
				case UhtEngineType.NativeInterface: return UhtFindOptions.Class;
				case UhtEngineType.ScriptStruct: return UhtFindOptions.ScriptStruct;
				case UhtEngineType.Enum: return UhtFindOptions.Enum;
				case UhtEngineType.Function: return UhtFindOptions.Function;
				case UhtEngineType.Delegate: return UhtFindOptions.DelegateFunction;
				case UhtEngineType.Property: return UhtFindOptions.Property;
				default:
					throw new UhtIceException("Invalid engine extended type");
			}
		}
	}

	[Flags]
	public enum UhtFindOptions
	{
		EngineName = 1 << 0,        // This is the default
		SourceName = 1 << 1,

		Enum = 1 << 8,
		ScriptStruct = 1 << 9,
		Class = 1 << 10,
		DelegateFunction = 1 << 11,
		Function = 1 << 12,
		Property = 1 << 13,

		NoParents = 1 << 16,
		NoOuter = 1 << 17,
		NoIncludes = 1 << 18,
		NoGlobal = 1 << 19,
		NoSelf = 1 << 20,
		CaseCompare = 1 << 21,
		CaselessCompare = 1 << 22,
		ParentsOnly = NoOuter | NoIncludes | NoGlobal | NoSelf,

		NamesMask = EngineName | SourceName,
		TypesMask = Enum | ScriptStruct | Class | DelegateFunction | Function | Property,
		ObjectTypesMask = Enum | ScriptStruct | Class,
		DelgeateTypesMask = DelegateFunction,
		PropertyOrFunction = Property | Function | DelegateFunction,
		SelfOnly = NoParents | NoOuter | NoIncludes | NoGlobal,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtFindOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtFindOptions InFlags, UhtFindOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtFindOptions InFlags, UhtFindOptions TestFlags)
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
		public static bool HasExactFlags(this UhtFindOptions InFlags, UhtFindOptions TestFlags, UhtFindOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	[Flags]
	public enum UhtValidationOptions
	{
		None,

		/// <summary>
		/// Test to see if any referenced types have been deprecated.  This should not
		/// be set when the parent object or property has already been marked as deprecated.
		/// </summary>
		Deprecated = 1 << 0,

		/// <summary>
		/// Test to see if the name of the property conflicts with another property in the super chain
		/// </summary>
		Shadowing = 1 << 1,

		/// <summary>
		/// The property is part of a key in a TMap
		/// </summary>
		IsKey = 1 << 2,

		/// <summary>
		/// The property is part of a value in a TArray, TSet, or TMap
		/// </summary>
		IsValue = 1 << 3,

		/// <summary>
		/// The property is either a key or a value part of a TArray, TSet, or TMap
		/// </summary>
		IsKeyOrValue = IsKey | IsValue,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtValidationOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtValidationOptions InFlags, UhtValidationOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtValidationOptions InFlags, UhtValidationOptions TestFlags)
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
		public static bool HasExactFlags(this UhtValidationOptions InFlags, UhtValidationOptions TestFlags, UhtValidationOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	public enum UhtAccessSpecifier
	{
		None,
		Public,
		Private,
		Protected,
	}

	public enum UhtResolvePhase : Int32
	{
		InvalidCheck = 1,
		Bases,
		Properties,
		Final,
		Count,
	}

	public class UhtDocumentationPolicy
	{
		private static UhtDocumentationPolicy StrictDocumentationPolicy = new UhtDocumentationPolicy
		{
			bPolicySet = true,
			bClassOrStructCommentRequired = true,
			bFunctionToolTipsRequired = true,
			bMemberToolTipsRequired = true,
			bParameterToolTipsRequired = true,
			bFloatRangesRequired = true,
		};

		private static UhtDocumentationPolicy NoDocumentationPolicy = new UhtDocumentationPolicy
		{
			bPolicySet = false,
			bClassOrStructCommentRequired = false,
			bFunctionToolTipsRequired = false,
			bMemberToolTipsRequired = false,
			bParameterToolTipsRequired = false,
			bFloatRangesRequired = false,
		};

		public bool bPolicySet;
		public bool bClassOrStructCommentRequired;
		public bool bFunctionToolTipsRequired;
		public bool bMemberToolTipsRequired;
		public bool bParameterToolTipsRequired;
		public bool bFloatRangesRequired;

		public static bool TryGet(UhtType Type, string DocumentationPolicyName, out UhtDocumentationPolicy Policy)
		{
			if (DocumentationPolicyName == "Strict")
			{
				Policy = StrictDocumentationPolicy;
				return true;
			}

			Policy = NoDocumentationPolicy;
			return DocumentationPolicyName.Length == 0;
		}
	};

	public abstract class UhtType : IUhtMessageSite, IUhtMessageLineNumber
	{
		/// <summary>
		/// Empty list of type used when children are requested but no children have been added.
		/// </summary>
		private static List<UhtType> EmptyTypeList = new List<UhtType>();

		/// <summary>
		/// All UHT runs are associated with a given session.  The session holds all the global information for a run.
		/// </summary>
		public UhtSession Session;

		/// <summary>
		/// Every type in a session is assigned a unique, non-zero id.
		/// </summary>
		public readonly int TypeIndex;

		/// <summary>
		/// The outer object containing this type.  For example, the header file would be the outer for a class defined in the global scope.
		/// </summary>
		public UhtType? Outer = null;

		/// <summary>
		/// The owning package of the type
		/// </summary>
		[JsonIgnore]
		public virtual UhtPackage Package
		{
			get
			{
				UhtType Type = this;
				while (Type.Outer != null)
				{
					Type = Type.Outer;
				}
				return (UhtPackage)Type;
			}
		}

		/// <summary>
		/// The header file containing the type
		/// </summary>
		[JsonIgnore]
		public virtual UhtHeaderFile HeaderFile
		{
			get
			{
				UhtType Type = this;
				while (Type.Outer != null && Type.Outer.Outer != null)
				{
					Type = Type.Outer;
				}
				return (UhtHeaderFile)Type;
			}
		}

		/// <summary>
		/// The name of the type as it appears in the source file
		/// </summary>
		public string SourceName { get; set; }

		/// <summary>
		/// The name of the type as used by the engines type system.  If not set, will default to the source name
		/// </summary>
		public string EngineName { get => this.EngineNameInternal ?? this.SourceName; set => this.EngineNameInternal = value; }

		/// <summary>
		/// Simple enumeration that can be used to detect the different types of objects
		/// </summary>
		[JsonIgnore]
		public abstract UhtEngineType EngineType { get; }

		/// <summary>
		/// The name of the engine's class for this type
		/// </summary>
		public abstract string EngineClassName { get; }

		/// <summary>
		/// If true, then this type is visible to the symbol table.
		/// </summary>
		public bool bVisibleType = true;

		/// <summary>
		/// Return true if the type has been deprecated
		/// </summary>
		[JsonIgnore]
		public virtual bool bDeprecated => false;

		/// <summary>
		/// The line number of where the definition began
		/// </summary>
		public int LineNumber { get; set; }

		/// <summary>
		/// Return a combination of the engine type name followed by the path name of the type
		/// </summary>
		[JsonIgnore]
		public virtual string FullName { get => GetFullName(); }

		/// <summary>
		/// Return the path name of the type which includes all parent outer objects excluding the header file
		/// </summary>
		[JsonIgnore]
		public string PathName { get => GetPathName(); }

		/// <summary>
		/// Return the name of the documentation policy to be used
		/// </summary>
		[JsonIgnore]
		public string DocumentationPolicyName { get => this.MetaData.GetValueOrDefaultHierarchical(UhtNames.DocumentationPolicy); }

		/// <summary>
		/// Return the specifier validation table to be used to validate the meta data on this type
		/// </summary>
		protected virtual UhtSpecifierValidatorTable? SpecifierValidatorTable { get => null; }

		/// <summary>
		/// Meta data associated with the type
		/// </summary>
		public UhtMetaData MetaData;

		/// <summary>
		/// Children types of this type
		/// </summary>
		[JsonConverter(typeof(UhtNullableTypeListJsonConverter<UhtType>))]
		public List<UhtType> Children { get => ChildrenInternal == null ? UhtType.EmptyTypeList : ChildrenInternal; }

		/// <summary>
		/// Helper to string method
		/// </summary>
		/// <returns></returns>
		public override string ToString() { return this.SourceName; }

		private string? EngineNameInternal = null;
		private List<UhtType>? ChildrenInternal = null;
		private int ResolveState = 0;

		#region IUHTMessageSite implementation
		[JsonIgnore]
		public virtual IUhtMessageSession MessageSession => this.HeaderFile.MessageSession;

		[JsonIgnore]
		public virtual IUhtMessageSource? MessageSource => this.HeaderFile.MessageSource;

		[JsonIgnore]
		public IUhtMessageLineNumber? MessageLineNumber => this;
		#endregion

		#region IUHTMessageListNumber implementation
		int IUhtMessageLineNumber.MessageLineNumber => LineNumber;
		#endregion

		/// <summary>
		/// Construct the base type information.  This constructor is used exclusively by UHTPackage which is at 
		/// the root of all type hierarchies.
		/// </summary>
		/// <param name="Session"></param>
		protected UhtType(UhtSession Session)
		{
			this.Session = Session;
			this.Outer = null;
			this.SourceName = string.Empty;
			this.LineNumber = 1;
			this.TypeIndex = Session.GetNextTypeIndex();
			this.MetaData = new UhtMetaData(this);
		}

		/// <summary>
		/// Construct instance of the type given a parent type.
		/// </summary>
		/// <param name="Outer">The outer type of the type being constructed.  For example, a class defined in a given header will have the header as the outer.</param>
		/// <param name="LineNumber">The line number in the source file where the type was defined.</param>
		/// <param name="MetaData">Optional meta data to be associated with the type.</param>
		protected UhtType(UhtType Outer, int LineNumber, UhtMetaData? MetaData = null)
		{
			this.Session = Outer.Session;
			this.Outer = Outer;
			this.SourceName = string.Empty;
			this.LineNumber = LineNumber;
			this.TypeIndex = this.Session.GetNextTypeIndex();
			this.MetaData = MetaData ?? new UhtMetaData(this);
			this.MetaData.MessageSite = this; // Make sure the site is correct
		}

		/// <summary>
		/// Add a type as a child
		/// </summary>
		/// <param name="Child">The child to be added.</param>
		public virtual void AddChild(UhtType Child)
		{
			if (this.ChildrenInternal == null)
			{
				this.ChildrenInternal = new List<UhtType>();
			}
			this.ChildrenInternal.Add(Child);
		}

		/// <summary>
		/// Return the list of children attached to the type.  The 
		/// list associated with the type is cleared.
		/// </summary>
		/// <returns>The current list of children</returns>
		public List<UhtType>? DetchChildren()
		{
			List<UhtType>? Out = this.ChildrenInternal;
			this.ChildrenInternal = null;
			return Out;
		}

		/// <summary>
		/// Merge the list children into the type.  The child's outer is set.
		/// </summary>
		/// <param name="Children">List of children to add.</param>
		public void AddChildren(List<UhtType>? Children)
		{
			if (Children != null)
			{
				foreach (UhtType Child in Children)
				{
					Child.Outer = this;
					AddChild(Child);
				}
			}
		}

		#region Type resolution
		public bool Resolve(UhtResolvePhase Phase)
		{
			int InitState = (int)Phase << 1;

			int CurrentState = Interlocked.Add(ref this.ResolveState, 0);
			
			// If we are already passed the init phase for this state, then we are good
			if (CurrentState > InitState)
			{
				return true;
			}

			// Attempt to make myself the resolve thread for this type.  If we aren't the ones
			// to get the init state, then we have to wait until the thread that did completes
			CurrentState = Interlocked.CompareExchange(ref this.ResolveState, InitState, CurrentState);
			if (CurrentState > InitState)
			{
				return true;
			}
			else if (CurrentState == InitState)
			{
				do
				{
					Thread.Yield();
					CurrentState = Interlocked.Add(ref this.ResolveState, 0);
				} while (CurrentState == InitState);
				return true;
			}
			else
			{
				bool bResult = true;
				try
				{
					ResolveSuper(Phase);
					bResult = ResolveSelf(Phase);
					ResolveChildren(Phase);
				}
				finally
				{
					Interlocked.Increment(ref this.ResolveState);
				}
				return bResult;
			}
		}

		protected virtual void ResolveSuper(UhtResolvePhase Phase)
		{
		}

		protected virtual bool ResolveSelf(UhtResolvePhase Phase)
		{
			return true;
		}

		protected virtual void ResolveChildren(UhtResolvePhase Phase)
		{
			if (this.ChildrenInternal != null)
			{
				if (Phase == UhtResolvePhase.InvalidCheck)
				{
					int OutIndex = 0;
					for (int Index = 0; Index < this.ChildrenInternal.Count; ++Index)
					{
						UhtType Child = this.ChildrenInternal[Index];
						if (Child.Resolve(Phase))
						{
							this.ChildrenInternal[OutIndex++] = Child;
						}
					}
					if (OutIndex < this.ChildrenInternal.Count)
					{
						this.ChildrenInternal.RemoveRange(OutIndex, this.ChildrenInternal.Count - OutIndex);
					}
				}
				else
				{
					foreach (UhtType Child in this.ChildrenInternal)
					{
						Child.Resolve(Phase);
					}
				}
			}
		}
		#endregion

		#region FindType support
		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Name">Name of the type.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <param name="LineNumber">Source code line number requesting the lookup.</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtFindOptions Options, string Name, IUhtMessageSite? MessageSite = null, int LineNumber = -1)
		{
			return this.Session.FindType(this, Options, Name, MessageSite, LineNumber);
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Name">Name of the type.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtFindOptions Options, ref UhtToken Name, IUhtMessageSite? MessageSite = null)
		{
			return this.Session.FindType(this, Options, ref Name, MessageSite);
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Identifiers">Enumeration of identifiers.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <param name="LineNumber">Source code line number requesting the lookup.</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtFindOptions Options, UhtTokenList Identifiers, IUhtMessageSite? MessageSite = null, int LineNumber = -1)
		{
			return this.Session.FindType(this, Options, Identifiers, MessageSite, LineNumber);
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Identifiers">Enumeration of identifiers.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <param name="LineNumber">Source code line number requesting the lookup.</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtFindOptions Options, UhtToken[] Identifiers, IUhtMessageSite? MessageSite = null, int LineNumber = -1)
		{
			return this.Session.FindType(this, Options, Identifiers, MessageSite, LineNumber);
		}
		#endregion

		#region Validation support
		// Validate the property settings
		protected virtual UhtValidationOptions Validate(UhtValidationOptions Options)
		{
			ValidateMetaData();
			ValidateDocumentationPolicy();
			return Options;
		}

		private void ValidateMetaData()
		{
			UhtSpecifierValidatorTable? Table = this.SpecifierValidatorTable;
			if (Table != null)
			{
				if (this.MetaData.Dictionary != null && this.MetaData.Dictionary.Count > 0)
				{
					foreach (var KVP in this.MetaData.Dictionary)
					{
						UhtSpecifierValidator? Specifier;
						if (Table.TryGetValue(KVP.Key.Name, out Specifier))
						{
							Specifier.Delegate(this, MetaData, KVP.Key, KVP.Value);
						}
					}
				}
			}
		}

		private void ValidateDocumentationPolicy()
		{
			if (UhtDocumentationPolicy.TryGet(this, this.DocumentationPolicyName, out UhtDocumentationPolicy Policy))
			{
				if (Policy.bPolicySet)
				{
					this.ValidateDocumentationPolicy(Policy);
				}
			}
			else
			{
				this.LogError(this.MetaData.LineNumber, $"Documentation policy '{this.DocumentationPolicyName}' is not known");
			}
		}

		protected virtual void ValidateDocumentationPolicy(UhtDocumentationPolicy Policy)
		{
		}

		public static void ValidateType(UhtType Type, UhtValidationOptions Options)
		{
			// Invoke the new method
			Options = Type.Validate(Options);

			// Invoke validate on the children
			foreach (UhtType Child in Type.Children)
			{
				ValidateType(Child, Options);
			}
		}
		#endregion

		#region Name and user facing text support
		public string GetPathName(UhtType? StopOuter = null)
		{
			StringBuilder Builder = new StringBuilder();
			GetPathName(Builder, StopOuter);
			return Builder.ToString();
		}

		public virtual void GetPathName(StringBuilder Builder, UhtType? StopOuter = null)
		{
			GetPathNameInternal(Builder, StopOuter);
			if (Builder.Length == 0)
			{
				Builder.Append("None");
			}
		}

		public virtual void GetPathNameInternal(StringBuilder Builder, UhtType? StopOuter = null)
		{
			if (this != StopOuter)
			{
				UhtType? Outer = this.Outer;
				if (Outer != null && Outer != StopOuter)
				{
					Outer.GetPathName(Builder, StopOuter);

					// SubObjectDelimiter is used to indicate that this object's outer is not a UPackage
					// In the C# version of UHT, we key off the header file
					if (!(Outer is UhtHeaderFile) && Outer.Outer is UhtHeaderFile)
					{
						Builder.Append(UhtFCString.SubObjectDelimiter);
					}

					// SubObjectDelimiter is also used to bridge between the object:properties in the path
					else if (!(Outer is UhtProperty) && this is UhtProperty)
					{
						Builder.Append(UhtFCString.SubObjectDelimiter);
					}
					else
					{
						Builder.Append('.');
					}
				}
				Builder.Append(this.EngineName);
			}
		}

		public string GetFullName()
		{
			StringBuilder Builder = new StringBuilder();
			GetFullName(Builder);
			return Builder.ToString();
		}

		public virtual void GetFullName(StringBuilder Builder)
		{
			Builder.Append(this.EngineClassName);
			Builder.Append(" ");
			GetPathName(Builder);
		}

		/// <summary>
		/// Finds the localized display name or native display name as a fallback.
		/// </summary>
		/// <returns>The display name for this object.</returns>
		public string GetDisplayNameText()
		{
			string? NativeDisplayName;
			if (this.MetaData.TryGetValue(UhtNames.DisplayName, out NativeDisplayName))
			{
				return NativeDisplayName;
			}

			return GetDisplayStringFromEngineName();
		}

		/// <summary>
		/// Finds the localized tooltip or native tooltip as a fallback.
		/// </summary>
		/// <param name="bShortToolTip">Look for a shorter version of the tooltip (falls back to the long tooltip if none was specified)</param>
		/// <returns>The tooltip for this object.</returns>
		public virtual string GetToolTipText(bool bShortToolTip = false)
		{
			string NativeToolTip = string.Empty;
			if (bShortToolTip)
			{
				NativeToolTip = this.MetaData.GetValueOrDefault(UhtNames.ShortToolTip);
			}

			if (NativeToolTip.Length == 0)
			{
				NativeToolTip = this.MetaData.GetValueOrDefault(UhtNames.ToolTip);
			}

			if (NativeToolTip.Length == 0)
			{
				return GetDisplayStringFromEngineName();
			}
			else
			{
				return NativeToolTip.TrimEnd().Replace("@see", "See:");
			}
		}

		/// <summary>
		/// Return a generated display string from the engine name
		/// </summary>
		/// <returns>The generated string</returns>
		protected virtual string GetDisplayStringFromEngineName()
		{
			return UhtFCString.NameToDisplayString(this.EngineName, false);
		}
		#endregion

		public virtual void CollectReferences(IUhtReferenceCollector Collector)
		{
		}
	}

	public static class UhtTypeStringBuilderExtensions
	{
		/// <summary>
		/// Append all of the outer names associated with the type
		/// </summary>
		/// <param name="Builder">Output string builder</param>
		/// <param name="Outer">Outer to be appended</param>
		/// <returns>Output string builder</returns>
		public static StringBuilder AppendOuterNames(this StringBuilder Builder, UhtType? Outer)
		{
			if (Outer == null)
			{
				return Builder;
			}

			if (Outer is UhtClass Class)
			{
				Builder.Append('_');
				Builder.Append(Outer.SourceName);
			}
			else if (Outer is UhtScriptStruct)
			{
				// Structs can also have UPackage outer.
				if (!(Outer.Outer is UhtHeaderFile))
				{
					AppendOuterNames(Builder, Outer.Outer);
				}
				Builder.Append('_');
				Builder.Append(Outer.SourceName);
			}
			else if (Outer is UhtPackage Package)
			{
				Builder.Append('_');
				Builder.Append(Package.ShortName);
			}
			else if (Outer is UhtHeaderFile)
			{
				// Pickup the package
				AppendOuterNames(Builder, Outer.Outer);
			}
			else
			{
				AppendOuterNames(Builder, Outer.Outer);
				Builder.Append('_');
				Builder.Append(Outer.EngineName);
			}

			return Builder;
		}
	}
}
