// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// Series of flags not part of the engine's function flags that affect code generation or verification
	/// </summary>
	[Flags]
	public enum UhtFunctionExportFlags : UInt32
	{
		None = 0,

		/// <summary>
		/// Function declaration included "final" keyword.  Used to differentiate between functions that have FUNC_Final only because they're private
		/// </summary>
		Final = 1 << 0,

		/// <summary>
		/// Function should be exported as a public API function
		/// </summary>
		RequiredAPI = 1 << 1,

		/// <summary>
		/// Export as an inline static C++ function
		/// </summary>
		Inline = 1 << 2,

		/// <summary>
		/// Export as a real C++ static function, causing thunks to call via ClassName::FuncName instead of this->FuncName
		/// </summary>
		CppStatic = 1 << 3,

		/// <summary>
		/// Export no thunk function; the user will manually define a custom one
		/// </summary>
		CustomThunk = 1 << 4,

		/// <summary>
		/// Function is marked as virtual
		/// </summary>
		Virtual = 1 << 5,

		/// <summary>
		/// The unreliable specified was present
		/// </summary>
		Unreliable = 1 << 6,

		/// <summary>
		/// The function is a sealed event
		/// </summary>
		SealedEvent = 1 << 7,

		/// <summary>
		/// Blueprint pure is being forced to false
		/// </summary>
		ForceBlueprintImpure = 1 << 8,

		/// <summary>
		/// If set, the BlueprintPure was automatically set
		/// </summary>
		AutoBlueprintPure = 1 << 9,

		/// <summary>
		/// If set, a method matching the CppImplName was found.  The search is only performed if it
		/// differs from the function name.
		/// </summary>
		ImplFound = 1 << 10,

		/// <summary>
		/// If the ImplFound flag is set, then if this flag is set, the method is virtual
		/// </summary>
		ImplVirtual = 1 << 11,

		/// <summary>
		/// If set, a method matching the CppValidationImplName was found.  The search is only performed if 
		/// CppImplName differs from the function name
		/// </summary>
		ValidationImplFound = 1 << 12,

		/// <summary>
		/// If the ValidationImplFound flag is set, then if this flag is set, the method id virtual.
		/// </summary>
		ValidationImplVirtual = 1 << 13,

		/// <summary>
		/// Set true if the function itself is declared const.  The is required for the automatic setting of BlueprintPure.
		/// </summary>
		DeclaredConst = 1 << 14,

		/// <summary>
		/// Final flag was set automatically and should not be considered for validation
		/// </summary>
		AutoFinal = 1 << 15,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtFunctionExportFlagsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtFunctionExportFlags InFlags, UhtFunctionExportFlags TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtFunctionExportFlags InFlags, UhtFunctionExportFlags TestFlags)
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
		public static bool HasExactFlags(this UhtFunctionExportFlags InFlags, UhtFunctionExportFlags TestFlags, UhtFunctionExportFlags MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	public enum UhtFunctionType
	{
		Function,
		Delegate,
		SparseDelegate,
	}

	public static class UhtFunctionTypeExtensions
	{
		public static bool IsDelegate(this UhtFunctionType FunctionType)
		{
			return FunctionType == UhtFunctionType.Delegate || FunctionType == UhtFunctionType.SparseDelegate;
		}
	}

	public class UhtFunction : UhtStruct
	{
		private static UhtSpecifierValidatorTable FunctionSpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.Function);
		private string? StrippedFunctionNameInternal = null;

		public static string GeneratedDelegateSignatureSuffix = "__DelegateSignature";

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EFunctionFlags FunctionFlags { get; set; } = EFunctionFlags.None;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtFunctionExportFlags FunctionExportFlags { get; set; } = UhtFunctionExportFlags.None;

		public UhtFunctionType FunctionType = UhtFunctionType.Function;

		public int FunctionLineNumber { get; set; } = 1;
		public int MacroLineNumber { get; set; } = 1;
		public string? SparseOwningClassName { get; set; } = null;
		public string? SparseDelegateName { get; set; } = null;

		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtFunction>))]
		public UhtFunction? SuperFunction => (UhtFunction?)this.Super;

		[JsonIgnore]
		public override UhtEngineType EngineType => this.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate) ? UhtEngineType.Delegate : UhtEngineType.Function;

		/// <inheritdoc/>
		public override string EngineClassName
		{
			get
			{
				switch (this.FunctionType)
				{
					case UhtFunctionType.Function:
						return "Function";
					case UhtFunctionType.Delegate:
						return "DelegateFunction";
					case UhtFunctionType.SparseDelegate:
						return "SparseDelegateFunction";
					default:
						throw new UhtIceException("Invalid function type");
				}
			}
		}

		[JsonIgnore]
		public string StrippedFunctionName
		{
			get
			{
				return this.StrippedFunctionNameInternal != null ? this.StrippedFunctionNameInternal : this.EngineName;
			}
			set 
			{
				this.StrippedFunctionNameInternal = value;
			}
		}

		///<inheritdoc/>
		[JsonIgnore]
		public override bool bDeprecated => this.MetaData.ContainsKey(UhtNames.DeprecatedFunction);

		///<inheritdoc/>
		[JsonIgnore]
		protected override UhtSpecifierValidatorTable? SpecifierValidatorTable { get => UhtFunction.FunctionSpecifierValidatorTable; }

		/// <summary>
		/// Identifier for an RPC call to a platform service
		/// </summary>
		public UInt16 RPCId = 0;

		/// <summary>
		/// Identifier for an RPC call expecting a response
		/// </summary>
		public UInt16 RPCResponseId = 0;

		/// <summary>
		/// Name of the actual implementation
		/// </summary>
		public string CppImplName = string.Empty;

		/// <summary>
		/// Name of the actual validation implementation
		/// </summary>
		public string CppValidationImplName = string.Empty;

		/// <summary>
		/// Name of the wrapper function that marshals the arguments and does the indirect call
		/// </summary>
		public string MarshalAndCallName = string.Empty;

		/// <summary>
		/// Name for callback-style names
		/// </summary>
		public string UnMarshalAndCallName = string.Empty;

		/// <summary>
		/// Endpoint name
		/// </summary>
		public string EndpointName = string.Empty;

		/// <summary>
		/// True if the function has a return value.
		/// </summary>
		[JsonIgnore]
		public bool bHasReturnProperty => this.Children.Count > 0 && ((UhtProperty)this.Children[this.Children.Count - 1]).PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm);

		/// <summary>
		/// The return value property or null if the function doesn't have a return value
		/// </summary>
		[JsonIgnore]
		public UhtProperty? ReturnProperty => this.bHasReturnProperty ? (UhtProperty)this.Children[this.Children.Count - 1] : null;

		/// <summary>
		/// True if the function has parameters.
		/// </summary>
		[JsonIgnore]
		public bool bHasParameters => this.Children.Count > 0 && !((UhtProperty)this.Children[0]).PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm);

		/// <summary>
		/// Return read only memory of all the function parameters
		/// </summary>
		[JsonIgnore]
		public ReadOnlyMemory<UhtType> ParameterProperties => new ReadOnlyMemory<UhtType>(this.Children.ToArray(), 0, this.bHasReturnProperty ? this.Children.Count - 1 : this.Children.Count);

		/// <summary>
		/// True if the function has any outputs including a return value
		/// </summary>
		[JsonIgnore]
		public bool bHasAnyOutputs
		{
			get
			{
				foreach (UhtType Type in this.Children)
				{
					if (Type is UhtProperty Property)
					{
						if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm | EPropertyFlags.OutParm))
						{
							return true;
						}
					}
				}
				return false;
			}
		}

		/// <summary>
		/// Construct a new instance of a function
		/// </summary>
		/// <param name="Outer">The parent object</param>
		/// <param name="LineNumber">The line number where the function is defined</param>
		public UhtFunction(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		/// <inheritdoc/>
		public override void AddChild(UhtType Type)
		{
			if (Type is UhtProperty Property)
			{
				if (!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.Parm))
				{
					throw new UhtIceException("Only properties marked as parameters can be added to functions");
				}
				if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm) && this.bHasReturnProperty)
				{
					throw new UhtIceException("Attempt to set multiple return properties on function");
				}
			}
			else
			{
				throw new UhtIceException("Invalid type added");
			}
			base.AddChild(Type);
		}

		#region Resolution support
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResults = base.ResolveSelf(Phase);

			switch (Phase)
			{
				case UhtResolvePhase.Bases:
					if (this.FunctionType == UhtFunctionType.Function && this.Outer is UhtClass OuterClass)
					{
						// non-static functions in a const class must be const themselves
						if (OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Const))
						{
							this.FunctionFlags |= EFunctionFlags.Const;
						}
					}
					break;

				case UhtResolvePhase.Final:
					if (this.Outer is UhtClass Class)
					{
						if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Native) &&
							!this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CustomThunk) &&
							this.CppImplName != this.SourceName)
						{
							UhtFoundDeclaration Declaration;
							if (Class.TryGetDeclaration(this.CppImplName, out Declaration))
							{
								this.FunctionExportFlags |= UhtFunctionExportFlags.ImplFound;
								if (Declaration.bIsVirtual)
								{
									this.FunctionExportFlags |= UhtFunctionExportFlags.ImplVirtual;
								}
							}
							if (Class.TryGetDeclaration(this.CppValidationImplName, out Declaration))
							{
								this.FunctionExportFlags |= UhtFunctionExportFlags.ValidationImplFound;
								if (Declaration.bIsVirtual)
								{
									this.FunctionExportFlags |= UhtFunctionExportFlags.ValidationImplVirtual;
								}
							}
						}

						if (this.FunctionType == UhtFunctionType.Function && this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.DeclaredConst))
						{
							// @todo: the presence of const and one or more outputs does not guarantee that there are
							// no side effects. On GCC and clang we could use __attribure__((pure)) or __attribute__((const))
							// or we could just rely on the use marking things BlueprintPure. Either way, checking the C++
							// const identifier to determine purity is not desirable. We should remove the following logic:

							// If its a const BlueprintCallable function with some sort of output and is not being marked as an BlueprintPure=false function, mark it as BlueprintPure as well
							if (this.bHasAnyOutputs && this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable) && 
								!this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.ForceBlueprintImpure))
							{
								this.FunctionFlags |= EFunctionFlags.BlueprintPure;
								this.FunctionExportFlags |= UhtFunctionExportFlags.AutoBlueprintPure; // Disable error for pure being set
							}
						}
					}

					// The following code is only performed on functions in a class.
					if (this.Outer is UhtClass)
					{
						foreach (UhtType Type in this.Children)
						{
							if (Type is UhtProperty Property)
							{
								if (Property.PropertyFlags.HasExactFlags(EPropertyFlags.OutParm | EPropertyFlags.ReturnParm, EPropertyFlags.OutParm))
								{
									this.FunctionFlags |= EFunctionFlags.HasOutParms;
								}
								if (Property is UhtStructProperty StructProperty)
								{
									if (StructProperty.ScriptStruct.bHasDefaults)
									{
										this.FunctionFlags |= EFunctionFlags.HasDefaults;
									}
								}
							}
						}
					}
					break;
			}
			return bResults;
		}
		#endregion

		#region Validation support
		protected override UhtValidationOptions Validate(UhtValidationOptions Options)
		{
			Options = base.Validate(Options);

			ValidateSpecifierFlags();

			// Some parameter test are optionally done
			bool bCheckForBlueprint = false;

			// Function type specific validation
			switch (this.FunctionType)
			{
				case UhtFunctionType.Function:
					UhtClass? OuterClass = (UhtClass?)this.Outer;

					if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure) && OuterClass != null && 
						OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Interface) && 
						!this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.AutoBlueprintPure))
					{
						this.LogError("BlueprintPure specifier is not allowed for interface functions");
					}

					// If this function is blueprint callable or blueprint pure, require a category 
					if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.BlueprintPure))
					{
						bool bBlueprintAccessor = this.MetaData.ContainsKey(UhtNames.BlueprintSetter) || this.MetaData.ContainsKey(UhtNames.BlueprintGetter);
						bool bHasMenuCategory = this.MetaData.ContainsKey(UhtNames.Category);
						bool bInternalOnly = this.MetaData.GetBoolean(UhtNames.BlueprintInternalUseOnly);

						if (!bHasMenuCategory && !bInternalOnly && !this.bDeprecated && !bBlueprintAccessor)
						{
							// To allow for quick iteration, don't enforce the requirement that game functions have to be categorized
							if (this.HeaderFile.Package.bIsPartOfEngine)
							{
								this.LogError("An explicit Category specifier is required for Blueprint accessible functions in an Engine module.");
							}
						}
					}

					// Process the virtual-ness
					if (this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.Virtual))
					{

						// if this is a BlueprintNativeEvent or BlueprintImplementableEvent in an interface, make sure it's not "virtual"
						if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
						{
							if (OuterClass != null && OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Interface))
							{
								this.LogError("BlueprintImplementableEvents in Interfaces must not be declared 'virtual'");
							}

							// if this is a BlueprintNativeEvent, make sure it's not "virtual"
							else if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
							{
								this.LogError("BlueprintNativeEvent functions must be non-virtual.");
							}

							else
							{
								this.LogWarning("BlueprintImplementableEvents should not be virtual. Use BlueprintNativeEvent instead.");
							}
						}
					}
					else
					{
						// if this is a function in an Interface, it must be marked 'virtual' unless it's an event
						if (OuterClass != null && OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Interface) && !this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
						{
							this.LogError("Interface functions that are not BlueprintImplementableEvents must be declared 'virtual'");
						}
					}

					// This is a final (prebinding, non-overridable) function
					if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Final) || this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.Final))
					{
						if (OuterClass != null && OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Interface))
						{
							this.LogError("Interface functions cannot be declared 'final'");
						}
						else if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent) && !this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.AutoFinal))
						{
							this.LogError("Blueprint events cannot be declared 'final'");
						}
					}

					if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.RequiredAPI) &&
						OuterClass != null && OuterClass.ClassFlags.HasAnyFlags(EClassFlags.RequiredAPI))
					{
						this.LogError($"API must not be used on methods of a class that is marked with an API itself.");
					}

					// Make sure that blueprint pure functions have some type of output
					if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure) && !this.bHasAnyOutputs)
					{
						this.LogError("BlueprintPure specifier is not allowed for functions with no return value and no output parameters.");
					}

					if (this.FunctionFlags.HasExactFlags(EFunctionFlags.Net | EFunctionFlags.NetRequest | EFunctionFlags.NetResponse, EFunctionFlags.Net) && this.ReturnProperty != null)
					{
						this.LogError("Replicated functions can't have return values");
					}

					bCheckForBlueprint = this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.BlueprintEvent);

					if (this.Outer is UhtStruct OuterStruct)
					{
						UhtStruct? OuterSuperStruct = OuterStruct.SuperStruct;
						if (OuterSuperStruct != null)
						{
							UhtType? OverriddenFunction = OuterSuperStruct.FindType(UhtFindOptions.SourceName | UhtFindOptions.Function, this.SourceName);
							if (OverriddenFunction != null)
							{
								// Native function overrides should be done in CPP text, not in a UFUNCTION() declaration (you can't change flags, and it'd otherwise be a burden to keep them identical)
								this.LogError(
									$"Override of UFUNCTION '{this.SourceName}' in parent '{OverriddenFunction.Outer?.SourceName}' cannot have a UFUNCTION() declaration above it; it will use the same parameters as the original declaration.");
							}
						}
					}

					// Make sure that the replication flags set on an overridden function match the parent function
					UhtFunction? SuperFunction = this.SuperFunction;
					if (SuperFunction != null)
					{
						if ((this.FunctionFlags & EFunctionFlags.NetFuncFlags) != (SuperFunction.FunctionFlags & EFunctionFlags.NetFuncFlags))
						{
							this.LogError($"Overridden function '{this.SourceName}' cannot specify different replication flags when overriding a function.");
						}
					}

					// if this function is an RPC in state scope, verify that it is an override
					// this is required because the networking code only checks the class for RPCs when initializing network data, not any states within it
					if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Net) && SuperFunction == null && !(this.Outer is UhtClass))
					{
						this.LogError($"Function '{this.SourceName}' base implementation of RPCs cannot be in a state. Add a stub outside state scope.");
					}

					// Check implemented RPC functions
					if (this.CppImplName != this.EngineName && 
						!this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CustomThunk) &&
						OuterClass!.GeneratedCodeVersion > EGeneratedCodeVersion.V1)
					{
						bool bIsNative = this.FunctionFlags.HasAnyFlags(EFunctionFlags.Native);
						bool bIsNet = this.FunctionFlags.HasAnyFlags(EFunctionFlags.Net);
						bool bIsNetValidate = this.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate);
						bool bIsNetResponse = this.FunctionFlags.HasAnyFlags(EFunctionFlags.NetResponse);
						bool bIsBlueprintEvent = this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent);

						bool bNeedsImplementation = (bIsNet && !bIsNetResponse) || bIsBlueprintEvent || bIsNative;
						bool bNeedsValidate = (bIsNative || bIsNet) && !bIsNetResponse && bIsNetValidate;

						bool bHasImplementation = this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.ImplFound);
						bool bHasValidate = this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.ValidationImplFound);
						bool bHasVirtualImplementation = this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.ImplVirtual);
						bool bHasVirtualValidate = this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.ValidationImplVirtual);

						if (bNeedsImplementation || bNeedsValidate)
						{
							// Check if functions are missing.
							if (bNeedsImplementation && !bHasImplementation)
							{
								LogRpcFunctionError(false, this.CppImplName);
							}

							if (bNeedsValidate && !bHasValidate)
							{
								LogRpcFunctionError(false, this.CppValidationImplName);
							}

							// If all needed functions are declared, check if they have virtual specifiers.
							if (bNeedsImplementation && bHasImplementation && !bHasVirtualImplementation)
							{
								LogRpcFunctionError(true, this.CppImplName);
							}

							if (bNeedsValidate && bHasValidate && !bHasVirtualValidate)
							{
								LogRpcFunctionError(true, this.CppValidationImplName);
							}
						}
					}
					break;

				case UhtFunctionType.Delegate:
				case UhtFunctionType.SparseDelegate:

					// Check for shadowing if requested
					if (Options.HasAnyFlags(UhtValidationOptions.Shadowing))
					{
						UhtType? Existing = this.Outer!.FindType(UhtFindOptions.DelegateFunction | UhtFindOptions.EngineName | UhtFindOptions.SelfOnly, this.EngineName);
						if (Existing != null && Existing != this)
						{
							this.LogError($"Can't override delegate signature function '{this.SourceName}'");
						}
					}
					break;
			}

			// Make sure this isn't a duplicate type.  In some ways this has already been check above
			UhtType? ExistingType = this.Outer!.FindType(UhtFindOptions.PropertyOrFunction | UhtFindOptions.EngineName | UhtFindOptions.SelfOnly, this.EngineName);
			if (ExistingType != null && ExistingType != this)
			{
				this.LogError($"'{this.EngineName}' conflicts with '{ExistingType.FullName}'");
			}

			// Do a more generic validation of the arguments
			foreach (UhtType Type in this.Children)
			{
				if (Type is UhtProperty Property)
				{
					// This code is technically incorrect but here for compatibility reasons.  Container types (TObjectPtr, TArray, etc...) should have the 
					// const on the inside of the template arguments but it places the const on the outside which isn't correct.  This needs to be addressed
					// in any updates to the type parser.  See EGetVarTypeOptions::NoAutoConst for the parser side of the problem.
					if (!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm) && Property.MustBeConstArgument(out UhtType? ErrorType))
					{
						if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm))
						{
							Property.LogError($"Return value must be 'const' since '{ErrorType.SourceName}' is marked 'const'");
						}
						else
						{
							Property.LogError($"Argument '{Property.SourceName}' must be 'const' since '{ErrorType.SourceName}' is marked 'const'");
						}
					}

					// Due to structures that might not be fully parsed at parse time, do blueprint validation here
					if (bCheckForBlueprint)
					{
						if (Property.bIsStaticArray)
						{
							this.LogError($"Static array cannot be exposed to blueprint. Function: {this.SourceName} Parameter {Property.SourceName}");
						}
						if (!Property.PropertyCaps.HasAnyFlags(UhtPropertyCaps.IsParameterSupportedByBlueprint))
						{
							this.LogError($"Type '{Property.GetUserFacingDecl()}' is not supported by blueprint. Function:  {this.SourceName} Parameter {Property.SourceName}");
						}
					}
				}
			}

			if (!this.bDeprecated)
			{
				Options |= UhtValidationOptions.Deprecated;
			}
			return Options;
		}

		private void LogRpcFunctionError(bool bVirtualError, string FunctionName)
		{
			StringBuilder Builder = new StringBuilder();

			if (bVirtualError)
			{
				Builder.Append("Declared function \"");
			}
			else
			{
				Builder.Append($"Function {this.SourceName} was marked as ");
				if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
				{
					Builder.Append("Native, ");
				}
				if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
				{
					Builder.Append("Net, ");
				}
				if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
				{
					Builder.Append("BlueprintEvent, ");
				}
				if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
				{
					Builder.Append("NetValidate, ");
				}
				Builder.Length = Builder.Length - 2;
				Builder.Append(". Declare function \"virtual ");
			}

			UhtProperty? ReturnType = this.ReturnProperty;
			if (ReturnType != null)
			{
				Builder.AppendPropertyText(ReturnType, UhtPropertyTextType.EventFunction);
				Builder.Append(' ');
			}
			else
			{
				Builder.Append("void ");
			}

			Builder.Append(this.Outer!.SourceName).Append("::").Append(FunctionName).Append('(');

			bool bFirst = true;
			foreach (UhtProperty Arg in this.ParameterProperties.Span)
			{
				if (!bFirst)
				{
					Builder.Append(", ");
				}
				bFirst = false;
				Builder.AppendFullDecl(Arg, UhtPropertyTextType.EventFunction, true);
			}

			Builder.Append(')');
			if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Const))
			{
				Builder.Append(" const");
			}

			if (bVirtualError)
			{
				Builder.Append("\" is not marked as virtual.");
			}
			else
			{
				Builder.Append("\"");
			}
			this.LogError(Builder.ToString());
		}

		private void ValidateSpecifierFlags()
		{
			if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Exec))
				{
					this.LogError("Replicated functions can not be blueprint events or exec");
				}

				bool bIsNetService = this.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest | EFunctionFlags.NetResponse);
				bool bIsNetReliable = this.FunctionFlags.HasAnyFlags(EFunctionFlags.NetReliable);

				if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.Static))
				{
					this.LogError("Static functions can't be replicated");
				}

				if (!bIsNetReliable && !this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.Unreliable) && !bIsNetService)
				{
					this.LogError("Replicated function: 'reliable' or 'unreliable' is required");
				}

				if (bIsNetReliable && this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.Unreliable) && !bIsNetService)
				{
					this.LogError("'reliable' and 'unreliable' are mutually exclusive");
				}
			}
			else if (this.FunctionFlags.HasAnyFlags(EFunctionFlags.NetReliable))
			{
				this.LogError("'reliable' specified without 'client' or 'server'");
			}
			else if (this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.Unreliable))
			{
				this.LogError("'unreliable' specified without 'client' or 'server'");
			}

			if (this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.SealedEvent) && !this.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
			{
				this.LogError("SealedEvent may only be used on events");
			}

			if (this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.SealedEvent) && this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				this.LogError("SealedEvent cannot be used on Blueprint events");
			}

			if (this.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.ForceBlueprintImpure) && this.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure))
			{
				this.LogError("BlueprintPure (or BlueprintPure=true) and BlueprintPure=false should not both appear on the same function, they are mutually exclusive");
			}
		}

		protected override void ValidateDocumentationPolicy(UhtDocumentationPolicy Policy)
		{
			if (this.FunctionType != UhtFunctionType.Function)
			{
				return;
			}

			UhtClass? OuterClass = (UhtClass?)this.Outer;

			if (Policy.bFunctionToolTipsRequired)
			{
				if (!this.MetaData.ContainsKey(UhtNames.ToolTip))
				{
					this.LogError($"Function '{OuterClass?.SourceName}::{this.SourceName}' does not provide a tooltip / comment (DocumentationPolicy).");
				}
			}

			if (Policy.bParameterToolTipsRequired)
			{
				if (!this.MetaData.ContainsKey(UhtNames.Comment))
				{
					this.LogError($"Function '{OuterClass?.SourceName}::{this.SourceName}' does not provide a comment (DocumentationPolicy).");

				}

				Dictionary<StringView, StringView> ParamToolTips = GetParameterToolTipsFromFunctionComment(this.MetaData.GetValueOrDefault(UhtNames.Comment));
				bool bHasAnyParamToolTips = ParamToolTips.Count > 1 || (ParamToolTips.Count == 1 && !ParamToolTips.ContainsKey(UhtNames.ReturnValue));

				// only apply the validation for parameter tooltips if a function has any @param statements at all.
				if (bHasAnyParamToolTips)
				{
					// ensure each parameter has a tooltip
					HashSet<StringView> ExistingFields = new HashSet<StringView>(StringViewComparer.OrdinalIgnoreCase);
					foreach (UhtType Parameter in this.ParameterProperties.Span)
					{
						ExistingFields.Add(Parameter.SourceName);
						if (!ParamToolTips.ContainsKey(Parameter.SourceName))
						{
							this.LogError($"Function '{OuterClass?.SourceName}::{this.SourceName}' doesn't provide a tooltip for parameter '{Parameter.SourceName}' (DocumentationPolicy).");
						}
					}

					// ensure we don't have parameter tooltips for parameters that don't exist
					foreach (KeyValuePair<StringView, StringView> Kvp in ParamToolTips)
					{
						if (Kvp.Key == UhtNames.ReturnValue)
						{
							continue;
						}

						if (!ExistingFields.Contains(Kvp.Key))
						{
							this.LogError($"Function '{OuterClass?.SourceName}::{this.SourceName}' provides a tooltip for an unknown parameter '{Kvp.Key}'");
						}
					}

					// check for duplicate tooltips
					Dictionary<StringView, StringView> ToolTipToParam = new Dictionary<StringView, StringView>(StringViewComparer.OrdinalIgnoreCase);
					foreach (KeyValuePair<StringView, StringView> Kvp in ParamToolTips)
					{
						if (Kvp.Key == UhtNames.ReturnValue)
						{
							continue;
						}

						if (ToolTipToParam.TryGetValue(Kvp.Value, out StringView Existing))
						{
							this.LogError($"Function '{OuterClass?.SourceName}::{this.SourceName}' uses identical tooltips for parameters '{Kvp.Key}' and '{Existing}' (DocumentationPolicy).");
						}
						else
						{
							ToolTipToParam.Add(Kvp.Value, Kvp.Key);
						}
					}
				}
			}
		}

		/// <summary>
		/// Add a parameter tooltip to the collection
		/// </summary>
		/// <param name="Name">Name of the parameter</param>
		/// <param name="Text">Text of the parameter tooltip</param>
		/// <param name="ParamMap">Parameter map to add the tooltip</param>
		private void AddParameterToolTip(StringView Name, StringView Text, Dictionary<StringView, StringView> ParamMap)
		{
			ParamMap.Add(Name, new StringView(UhtFCString.TabsToSpaces(Text.Memory.Trim(), 4, false)));
		}

		/// <summary>
		/// Add a parameter tooltip to the collection where the name of the parameter is the first word of the line
		/// </summary>
		/// <param name="Text">Text of the parameter tooltip</param>
		/// <param name="ParamMap">Parameter map to add the tooltip</param>
		private void AddParameterToolTip(StringView Text, Dictionary<StringView, StringView> ParamMap)
		{
			ReadOnlyMemory<char> Trimmed = Text.Memory.Trim();
			int NameEnd = Trimmed.Span.IndexOf(' ');
			if (NameEnd >= 0)
			{
				AddParameterToolTip(new StringView(Trimmed, 0, NameEnd), new StringView(Trimmed, NameEnd + 1), ParamMap);
			}
		}

		/// <summary>
		/// Parse the function comment looking for parameter documentation.
		/// </summary>
		/// <param name="Input">The function input comment</param>
		/// <returns>Dictionary of parameter names and the documentation.  The return value will have a name of "ReturnValue"</returns>
		private Dictionary<StringView, StringView> GetParameterToolTipsFromFunctionComment(StringView Input)
		{
			const string ParamTag = "@param";
			const string ReturnTag = "@return";

			Dictionary<StringView, StringView> ParamMap = new Dictionary<StringView, StringView>(StringViewComparer.OrdinalIgnoreCase);

			// Loop until we are out of text
			while (Input.Length > 0)
			{

				// Extract the line
				StringView Line = Input;
				int LineEnd = Input.Span.IndexOf('\n');
				if (LineEnd >= 0)
				{
					Line = new StringView(Input, 0, LineEnd);
					Input = new StringView(Input, LineEnd + 1);
				}

				// If this is a parameter, invoke the method to extract the name first
				int ParamStart = Line.Span.IndexOf(ParamTag, StringComparison.OrdinalIgnoreCase);
				if (ParamStart >= 0)
				{
					AddParameterToolTip(new StringView(Line, ParamStart + ParamTag.Length), ParamMap);
					continue;
				}

				// If this is the return value, we have a known name.  Invoke the method where we already know the name
				ParamStart = Line.Span.IndexOf(ReturnTag, StringComparison.OrdinalIgnoreCase);
				if (ParamStart >= 0)
				{
					AddParameterToolTip(new StringView(UhtNames.ReturnValue), new StringView(Line, ParamStart + ReturnTag.Length), ParamMap);
				}
			}

			return ParamMap;
		}
		#endregion

		/// <inheritdoc/>
		public override void CollectReferences(IUhtReferenceCollector Collector)
		{
			if (this.FunctionType != UhtFunctionType.Function)
			{
				Collector.AddExportType(this);
				Collector.AddSingleton(this);
				Collector.AddDeclaration(this, true);
				Collector.AddCrossModuleReference(this, true);
				Collector.AddCrossModuleReference(this.Package, true);
			}
			foreach (UhtType Child in this.Children)
			{
				Child.CollectReferences(Collector);
			}
		}
	}
}
