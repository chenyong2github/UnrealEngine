// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// Flags to represent information about a RigVM parameter
	/// </summary>
	[Flags]
	public enum UhtRigVMParameterFlags : UInt32
	{
		/// <summary>
		/// No RigVM flags
		/// </summary>
		None = 0,

		/// <summary>
		/// "Constant" metadata specified
		/// </summary>
		Constant = 0x00000001,

		/// <summary>
		/// "Input" metadata specified
		/// </summary>
		Input = 0x00000002,

		/// <summary>
		/// "Output" metadata specified
		/// </summary>
		Output = 0x00000004,

		/// <summary>
		/// "Singleton" metadata specified
		/// </summary>
		Singleton = 0x00000008,

		/// <summary>
		/// Set if the property is editor only
		/// </summary>
		EditorOnly		= 0x00000010,

		/// <summary>
		/// Set if the property is an enum
		/// </summary>
		IsEnum = 0x00010000,

		/// <summary>
		/// Set if the property is an array
		/// </summary>
		IsArray = 0x00020000,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtRigVMParameterFlagsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtRigVMParameterFlags InFlags, UhtRigVMParameterFlags TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtRigVMParameterFlags InFlags, UhtRigVMParameterFlags TestFlags)
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
		public static bool HasExactFlags(this UhtRigVMParameterFlags InFlags, UhtRigVMParameterFlags TestFlags, UhtRigVMParameterFlags MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// The FRigVMParameter represents a single parameter of a method
	/// marked up with RIGVM_METHOD.
	/// Each parameter can be marked with Constant, Input or Output
	/// metadata - this struct simplifies access to that information.
	/// </summary>
	public class UhtRigVMParameter
	{
		/// <summary>
		/// Property associated with the RigVM parameter
		/// </summary>
		public readonly UhtProperty? Property;

		/// <summary>
		/// Name of the property
		/// </summary>
		public string Name { get; } = String.Empty;

		/// <summary>
		/// Type of the property
		/// </summary>
		public string Type { get; } = String.Empty;

		/// <summary>
		/// Cast name
		/// </summary>
		public readonly string? CastName = null;

		/// <summary>
		/// Cast type
		/// </summary>
		public readonly string? CastType = null;

		/// <summary>
		/// Flags associated with the parameter
		/// </summary>
		public UhtRigVMParameterFlags ParameterFlags = UhtRigVMParameterFlags.None;

		/// <summary>
		/// True if the parameter is marked as "Constant"
		/// </summary>
		[JsonIgnore]
		public bool bConstant => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Constant);

		/// <summary>
		/// True if the parameter is marked as "Input"
		/// </summary>
		[JsonIgnore]
		public bool bInput => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Input);

		/// <summary>
		/// True if the parameter is marked as "Output"
		/// </summary>
		[JsonIgnore]
		public bool bOutput => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Output);

		/// <summary>
		/// True if the parameter is marked as "Singleton"
		/// </summary>
		[JsonIgnore]
		public bool bSingleton => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Singleton);

		/// <summary>
		/// True if the parameter is editor only
		/// </summary>
		[JsonIgnore]
		public bool bEditorOnly => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.EditorOnly);

		/// <summary>
		/// True if the parameter is an enum
		/// </summary>
		[JsonIgnore]
		public bool bIsEnum => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.IsEnum);

		/// <summary>
		/// True if the parameter is an array
		/// </summary>
		[JsonIgnore]
		public bool bIsArray => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.IsArray);

		/// <summary>
		/// Create a new RigVM parameter from a property
		/// </summary>
		/// <param name="Property">Source property</param>
		/// <param name="Index">Parameter index.  Used to create a unique cast name.</param>
		public UhtRigVMParameter(UhtProperty Property, int Index)
		{
			this.Property = Property;

			this.Name = Property.EngineName;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Constant") ? UhtRigVMParameterFlags.Constant : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Input") ? UhtRigVMParameterFlags.Input : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Output") ? UhtRigVMParameterFlags.Output : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.bIsEditorOnlyProperty ? UhtRigVMParameterFlags.EditorOnly : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Singleton") ? UhtRigVMParameterFlags.Singleton : UhtRigVMParameterFlags.None;

			if (Property.MetaData.ContainsKey("Visible"))
			{
				this.ParameterFlags |= UhtRigVMParameterFlags.Constant;
				this.ParameterFlags &= (UhtRigVMParameterFlags.Input | UhtRigVMParameterFlags.Output);
			}

			if (this.bEditorOnly)
			{
				Property.LogError($"RigVM Struct '{this.Property.Outer?.SourceName}' - Member '{this.Property.SourceName}' is editor only - WITH_EDITORONLY_DATA not allowed on structs with RIGVM_METHOD.");
			}

			string? RigVMType = Property.GetRigVMType(ref this.ParameterFlags);
			if (RigVMType == null)
			{
				Property.LogError($"RigVM Struct '{this.Property.Outer?.SourceName}' - Member '{this.Property.SourceName}' type '{this.Property.GetUserFacingDecl()}' not supported by RigVM.");
			}
			else
			{
				this.Type = RigVMType;
			}

			if (this.bIsArray)
			{
				if (this.IsConst())
				{
					string ExtendedType = this.ExtendedType(false);
					this.CastName = $"{this.Name}_{Index}_Array";
					this.CastType = $"TArrayView<const {ExtendedType.Substring(1, ExtendedType.Length - 2)}>";
				}
			}
		}

		/// <summary>
		/// Create a new parameter
		/// </summary>
		/// <param name="Name">Name of the parameter</param>
		/// <param name="Type">Type of the parameter</param>
		public UhtRigVMParameter(string Name, string Type)
		{
			this.Name = Name;
			this.Type = Type;
		}

		/// <summary>
		/// Get the name of the parameter
		/// </summary>
		/// <param name="bCastName">If true, return the cast name</param>
		/// <returns>Parameter name</returns>
		public string NameOriginal(bool bCastName = false)
		{
			return bCastName && this.CastName != null ? this.CastName : this.Name;
		}

		/// <summary>
		/// Get the type of the parameter
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <returns>Parameter type</returns>
		public string TypeOriginal(bool bCastType = false)
		{
			return bCastType && this.CastType != null ? this.CastType : this.Type;
		}

		/// <summary>
		/// Get the full declaration (type and name)
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <param name="bCastName">If true, return the cast name</param>
		/// <returns>Parameter declaration</returns>
		public string Declaration(bool bCastType = false, bool bCastName = false)
		{
			return $"{TypeOriginal(bCastType)} {NameOriginal(bCastName)}";
		}

		/// <summary>
		/// Return the base type without any template arguments
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <returns>Base parameter type</returns>
		public string BaseType(bool bCastType = false)
		{
			string String = TypeOriginal(bCastType);

			int LesserPos = String.IndexOf('<');
			if (LesserPos >= 0)
			{
				return String.Substring(0, LesserPos);
			}
			else
			{
				return String;
			}
		}

		/// <summary>
		/// Template arguments of the type or type if not a template type.
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <returns>Template arguments of the type</returns>
		public string ExtendedType(bool bCastType = false)
		{
			string String = TypeOriginal(bCastType);

			int LesserPos = String.IndexOf('<');
			if (LesserPos >= 0)
			{
				return String.Substring(LesserPos);
			}
			else
			{
				return String;
			}
		}

		/// <summary>
		/// Return the type with a const reference
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <returns>Type with a const reference</returns>
		public string TypeConstRef(bool bCastType = false)
		{
			string String = TypeNoRef(bCastType);
			if (String.StartsWith("T") || String.StartsWith("F"))
			{
				return $"const {String}&";
			}
			else
			{
				return $"const {String}";
			}
		}

		/// <summary>
		/// Return the type with a reference
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <returns>Type with a reference</returns>
		public string TypeRef(bool bCastType = false)
		{
			string String = TypeNoRef(bCastType);
			return $"{String}&";
		}

		/// <summary>
		/// Return the type without reference
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <returns>Type without the reference</returns>
		public string TypeNoRef(bool bCastType = false)
		{
			string String = TypeOriginal(bCastType);
			if (String.EndsWith("&"))
			{
				return String.Substring(0, String.Length - 1);
			}
			else
			{
				return String;
			}
		}

		/// <summary>
		/// Return the type as a reference
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <returns>Type as a reference</returns>
		public string TypeVariableRef(bool bCastType = false)
		{
			return IsConst() ? TypeConstRef(bCastType) : TypeRef(bCastType);
		}

		/// <summary>
		/// Return a variable declaration for the parameter
		/// </summary>
		/// <param name="bCastType">If true, return the cast type</param>
		/// <param name="bCastName">If true, return the cast name</param>
		/// <returns>Parameter as a variable declaration</returns>
		public string Variable(bool bCastType = false, bool bCastName = false)
		{
			return $"{TypeVariableRef(bCastType)} {NameOriginal(bCastName)}";
		}

		/// <summary>
		/// True if the parameter is constant
		/// </summary>
		/// <returns>True if the parameter is constant</returns>
		public bool IsConst()
		{
			return this.bConstant || (this.bInput && !this.bOutput);
		}

		/// <summary>
		/// Return true if the parameter requires a cast
		/// </summary>
		/// <returns>True if the parameter requires a cast</returns>
		public bool RequiresCast()
		{
			return this.CastType != null && this.CastName != null;
		}
	}

	/// <summary>
	/// A single info dataset for a function marked with RIGVM_METHOD.
	/// This struct provides access to its name, the return type and all parameters.
	/// </summary>
	public class UhtRigVMMethodInfo
	{
		private static string NoPrefixInternal = string.Empty;
		private static string ReturnPrefixInternal = "return ";

		/// <summary>
		/// Return type of the method
		/// </summary>
		public string ReturnType { get; set; } = string.Empty;

		/// <summary>
		/// Name of the method
		/// </summary>
		public string Name { get; set; } = string.Empty;

		/// <summary>
		/// Method parameters
		/// </summary>
		public List<UhtRigVMParameter> Parameters { get; set; } = new List<UhtRigVMParameter>();

		/// <summary>
		/// If the method has a return value, return "return".  Otherwise return nothing.
		/// </summary>
		/// <returns>Prefix required for the return value</returns>
		public string ReturnPrefix()
		{
			return (ReturnType.Length == 0 || ReturnType == "void") ? NoPrefixInternal : ReturnPrefixInternal;
		}
	}

	/// <summary>
	/// An info dataset providing access to all functions marked with RIGVM_METHOD
	/// for each struct.
	/// </summary>
	public class UhtRigVMStructInfo
	{

		/// <summary>
		/// True if the GetUpgradeInfoMethod was found. 
		/// </summary>
		public bool bHasGetUpgradeInfoMethod = false;

		/// <summary>
		/// Engine name of the owning script struct
		/// </summary>
		public string Name { get; set; } = string.Empty;

		/// <summary>
		/// List of the members
		/// </summary>
		public List<UhtRigVMParameter> Members = new List<UhtRigVMParameter>();

		/// <summary>
		/// List of the methods
		/// </summary>
		public List<UhtRigVMMethodInfo> Methods { get; set; } = new List<UhtRigVMMethodInfo>();
	};

	/// <summary>
	/// Series of flags not part of the engine's script struct flags that affect code generation or verification
	/// </summary>
	[Flags]
	public enum UhtScriptStructExportFlags : UInt32
	{

		/// <summary>
		/// No export flags
		/// </summary>
		None = 0,

		/// <summary>
		/// "HasDefaults" specifier present
		/// </summary>
		HasDefaults = 1 << 0,

		/// <summary>
		/// "HasNoOpConstructor" specifier present
		/// </summary>
		HasNoOpConstructor = 1 << 1,

		/// <summary>
		/// "IsAlwaysAccessible" specifier present
		/// </summary>
		IsAlwaysAccessible = 1 << 2,

		/// <summary>
		/// "IsCoreType" specifier present
		/// </summary>
		IsCoreType = 1 << 3,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtScriptStructExportFlagsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtScriptStructExportFlags InFlags, UhtScriptStructExportFlags TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtScriptStructExportFlags InFlags, UhtScriptStructExportFlags TestFlags)
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
		public static bool HasExactFlags(this UhtScriptStructExportFlags InFlags, UhtScriptStructExportFlags TestFlags, UhtScriptStructExportFlags MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// Represents the USTRUCT object
	/// </summary>
	[UhtEngineClass(Name = "ScriptStruct")]
	public class UhtScriptStruct : UhtStruct
	{
		private static UhtSpecifierValidatorTable ScriptStructSpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.ScriptStruct);

		/// <summary>
		/// Script struct engine flags
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EStructFlags ScriptStructFlags { get; set; } = EStructFlags.NoFlags;

		/// <summary>
		/// UHT only script struct falgs
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtScriptStructExportFlags ScriptStructExportFlags { get; set; } = UhtScriptStructExportFlags.None;

		/// <summary>
		/// Line number where GENERATED_BODY/GENERATED_USTRUCT_BODY macro was found
		/// </summary>
		public int MacroDeclaredLineNumber { get; set; } = -1;

		/// <summary>
		/// RigVM structure info
		/// </summary>
		public UhtRigVMStructInfo? RigVMStructInfo { get; set; } = null;

		/// <inheritdoc/>
		[JsonIgnore]
		public override UhtEngineType EngineType => UhtEngineType.ScriptStruct;

		/// <summary>
		/// True if the struct has the "HasDefaults" specifier
		/// </summary>
		[JsonIgnore]
		public bool bHasDefaults => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.HasDefaults);

		/// <summary>
		/// True if the struct has the "IsAlwaysAccessible" specifier
		/// </summary>
		[JsonIgnore]
		public bool bIsAlwaysAccessible => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.IsAlwaysAccessible);

		/// <summary>
		/// True if the struct has the "IsCoreType" specifier
		/// </summary>
		[JsonIgnore]
		public bool bIsCoreType => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.IsCoreType);

		/// <summary>
		/// True if the struct has the "HasNoOpConstructor" specifier
		/// </summary>
		[JsonIgnore]
		public bool bHasNoOpConstructor => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.HasNoOpConstructor);

		/// <inheritdoc/>
		public override string EngineClassName { get => "ScriptStruct"; }

		/// <inheritdoc/>
		[JsonIgnore]
		public override string EngineNamePrefix
		{
			get
			{
				return UhtConfig.Instance.IsStructWithTPrefix(this.EngineName) ? "T" : "F";
			}
		}

		///<inheritdoc/>
		[JsonIgnore]
		protected override UhtSpecifierValidatorTable? SpecifierValidatorTable { get => UhtScriptStruct.ScriptStructSpecifierValidatorTable; }

		/// <summary>
		/// Super struct
		/// </summary>
		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtScriptStruct>))]
		public UhtScriptStruct? SuperScriptStruct => (UhtScriptStruct?)this.Super;

		/// <summary>
		/// Construct a new script struct
		/// </summary>
		/// <param name="Outer">Outer type</param>
		/// <param name="LineNumber">Line number of the definition</param>
		public UhtScriptStruct(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
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
						this.ScriptStructFlags |= EStructFlags.HasInstancedReference;
					}
					CollectRigVMMembers();
					break;
			}
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool bDeepScan)
		{
			if (this.ScriptStructFlags.HasAnyFlags(EStructFlags.HasInstancedReference))
			{
				return true;
			}

			if (this.SuperScriptStruct != null && this.SuperScriptStruct.ScanForInstancedReferenced(bDeepScan))
			{
				return true;
			}

			return base.ScanForInstancedReferenced(bDeepScan);
		}

		#region Validation support
		/// <inheritdoc/>
		protected override UhtValidationOptions Validate(UhtValidationOptions Options)
		{
			Options = base.Validate(Options);

			if (this.ScriptStructFlags.HasAnyFlags(EStructFlags.Immutable))
			{
				if (this.HeaderFile != this.Session.UObject.HeaderFile)
				{
					this.LogError("Immutable is being phased out in favor of SerializeNative, and is only legal on the mirror structs declared in UObject");
				}
			}

			// Validate the engine name
			string ExpectedName = $"{this.EngineNamePrefix}{this.EngineName}";
			if (this.SourceName != ExpectedName)
			{
				this.LogError($"Struct '{this.SourceName}' has an invalid Unreal prefix, expecting '{ExpectedName}");
			}

			// Validate RigVM
			if (this.RigVMStructInfo != null && this.MetaData.ContainsKey("Deprecated") && !this.RigVMStructInfo.bHasGetUpgradeInfoMethod)
			{
				this.LogError($"RigVMStruct '{this.SourceName}' is marked as deprecated but is missing 'GetUpgradeInfo method.");
				this.LogError("Please implement a method like below:");
				this.LogError("RIGVM_METHOD()");
				this.LogError("virtual FRigVMStructUpgradeInfo GetUpgradeInfo() const override;");					
			}

			return Options |= UhtValidationOptions.Shadowing;
		}
		#endregion

		/// <inheritdoc/>
		public override void CollectReferences(IUhtReferenceCollector Collector)
		{
			if (this.ScriptStructFlags.HasAnyFlags(EStructFlags.NoExport))
			{
				Collector.AddSingleton(this);
			}
			Collector.AddExportType(this);
			Collector.AddDeclaration(this, true);
			Collector.AddCrossModuleReference(this, true);
			if (this.SuperScriptStruct != null)
			{
				Collector.AddCrossModuleReference(this.SuperScriptStruct, true);
			}
			Collector.AddCrossModuleReference(this.Package, true);
			foreach (UhtType Child in this.Children)
			{
				Child.CollectReferences(Collector);
			}
		}

		private void CollectRigVMMembers()
		{
			if (this.RigVMStructInfo != null)
			{
				for (UhtStruct? Current = this; Current != null; Current = Current.SuperStruct)
				{
					foreach (UhtProperty Property in Current.Properties)
					{
						this.RigVMStructInfo.Members.Add(new UhtRigVMParameter(Property, this.RigVMStructInfo.Members.Count));
					}
				}

				if (this.RigVMStructInfo.Members.Count == 0)
				{
					this.LogError($"RigVM Struct '{this.SourceName}' - has zero members - invalid RIGVM_METHOD.");
				}
				else if (this.RigVMStructInfo.Members.Count > 64)
				{
					this.LogError($"RigVM Struct '{this.SourceName}' - has {this.RigVMStructInfo.Members.Count} members (64 is the limit).");
				}
			}
		}
	}
}