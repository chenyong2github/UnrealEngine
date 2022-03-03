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
		None			= 0,
		Constant		= 0x00000001,
		Input			= 0x00000002,
		Output			= 0x00000004,
		Singleton		= 0x00000008,
		EditorOnly		= 0x00000010,
		IsEnum			= 0x00010000,
		IsArray			= 0x00020000,
		IsFixedArray	= 0x00040000,
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

	/**
	 * The FRigVMParameter represents a single parameter of a method
	 * marked up with RIGVM_METHOD.
	 * Each parameter can be marked with Constant, Input or Output
	 * metadata - this struct simplifies access to that information.
	 */

	public class UhtRigVMParameter
	{
		public readonly UhtProperty? Property;
		public string Name { get; } = String.Empty;
		public string Type { get; } = String.Empty;
		public readonly string Getter = String.Empty;
		public readonly string? CastName = null;
		public readonly string? CastType = null;
		public UhtRigVMParameterFlags ParameterFlags = UhtRigVMParameterFlags.None;

		[JsonIgnore]
		public bool bConstant => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Constant);

		[JsonIgnore]
		public bool bInput => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Input);

		[JsonIgnore]
		public bool bOutput => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Output);

		[JsonIgnore]
		public bool bSingleton => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.Singleton);

		[JsonIgnore]
		public bool bEditorOnly => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.EditorOnly);

		[JsonIgnore]
		public bool bIsEnum => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.IsEnum);

		[JsonIgnore]
		public bool bIsArray => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.IsArray);

		[JsonIgnore]
		public bool bIsFixedArray => this.ParameterFlags.HasAnyFlags(UhtRigVMParameterFlags.IsFixedArray);

		public UhtRigVMParameter(UhtProperty Property, int Index)
		{
			this.Property = Property;

			this.Name = Property.EngineName;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Constant") ? UhtRigVMParameterFlags.Constant : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Input") ? UhtRigVMParameterFlags.Input : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Output") ? UhtRigVMParameterFlags.Output : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.bIsEditorOnlyProperty ? UhtRigVMParameterFlags.EditorOnly : UhtRigVMParameterFlags.None;
			this.ParameterFlags |= Property.MetaData.ContainsKey("Singleton") ? UhtRigVMParameterFlags.Singleton : UhtRigVMParameterFlags.None;
			this.Getter = "GetRef";

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
					this.CastType = $"TArrayView<const {ExtendedType.AsMemory(1, ExtendedType.Length - 2)}>";
				}
			}
		}

		public UhtRigVMParameter(string Name, string Type)
		{
			this.Name = Name;
			this.Type = Type;
		}

		public string NameOriginal(bool bCastName = false)
		{
			return bCastName && this.CastName != null ? this.CastName : this.Name;
		}

		public string TypeOriginal(bool bCastType = false)
		{
			return bCastType && this.CastType != null ? this.CastType : this.Type;
		}

		public string Declaration(bool bCastType = false, bool bCastName = false)
		{
			return $"{TypeOriginal(bCastType)} {NameOriginal(bCastName)}";
		}

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

		public string TypeRef(bool bCastType = false)
		{
			string String = TypeNoRef(bCastType);
			return $"{String}&";
		}

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

		public string TypeVariableRef(bool bCastType = false)
		{
			return IsConst() ? TypeConstRef(bCastType) : TypeRef(bCastType);
		}

		public string Variable(bool bCastType = false, bool bCastName = false)
		{
			return $"{TypeVariableRef(bCastType)} {NameOriginal(bCastName)}";
		}

		public bool IsConst()
		{
			return this.bConstant || (this.bInput && !this.bOutput);
		}

		public bool RequiresCast()
		{
			return this.CastType != null && this.CastName != null;
		}
	}

	/**
	 * A single info dataset for a function marked with RIGVM_METHOD.
	 * This struct provides access to its name, the return type and all parameters.
	 */
	public class UhtRigVMMethodInfo
	{
		private static string NoPrefixInternal = string.Empty;
		private static string ReturnPrefixInternal = "return ";

		public string ReturnType { get; set; } = string.Empty;
		public string Name { get; set; } = string.Empty;
		public List<UhtRigVMParameter> Parameters { get; set; } = new List<UhtRigVMParameter>();

		public string ReturnPrefix()
		{
			return (ReturnType.Length == 0 || ReturnType == "void") ? NoPrefixInternal : ReturnPrefixInternal;
		}
	}

	/**
	 * An info dataset providing access to all functions marked with RIGVM_METHOD
	 * for each struct.
	 */
	public class UhtRigVMStructInfo
	{
		public string Name { get; set; } = string.Empty;
		public List<UhtRigVMParameter> Members = new List<UhtRigVMParameter>();
		public List<UhtRigVMMethodInfo> Methods { get; set; } = new List<UhtRigVMMethodInfo>();
	};

	/// <summary>
	/// Series of flags not part of the engine's script struct flags that affect code generation or verification
	/// </summary>
	[Flags]
	public enum UhtScriptStructExportFlags : UInt32
	{
		None = 0,
		HasDefaults = 1 << 0,
		HasNoOpConstructor = 1 << 1,
		IsAlwaysAccessible = 1 << 2,
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


	[UhtEngineClass(Name = "ScriptStruct")]
	public class UhtScriptStruct : UhtStruct
	{
		private static UhtSpecifierValidatorTable ScriptStructSpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.ScriptStruct);

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EStructFlags ScriptStructFlags { get; set; } = EStructFlags.NoFlags;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtScriptStructExportFlags ScriptStructExportFlags { get; set; } = UhtScriptStructExportFlags.None;

		public int MacroDeclaredLineNumber { get; set; } = -1;

		public UhtRigVMStructInfo? RigVMStructInfo { get; set; } = null;

		[JsonIgnore]
		public override UhtEngineType EngineType => UhtEngineType.ScriptStruct;

		[JsonIgnore]
		public bool bHasDefaults => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.HasDefaults);

		[JsonIgnore]
		public bool bIsAlwaysAccessible => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.IsAlwaysAccessible);

		[JsonIgnore]
		public bool bIsCoreType => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.IsCoreType);

		[JsonIgnore]
		public bool bHasNoOpConstructor => this.ScriptStructExportFlags.HasAnyFlags(UhtScriptStructExportFlags.HasNoOpConstructor);

		/// <inheritdoc/>
		public override string EngineClassName { get => "ScriptStruct"; }

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

		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtScriptStruct>))]
		public UhtScriptStruct? SuperScriptStruct => (UhtScriptStruct?)this.Super;

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