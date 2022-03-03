// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	[UhtEngineClass(Name = "Struct")]
	public abstract class UhtStruct : UhtField
	{
		public EGeneratedCodeVersion GeneratedCodeVersion = EGeneratedCodeVersion.None;

		/// <summary>
		/// Return a collection of children that are properties
		/// 
		/// NOTE: This method allocates memory to construct the enumerator.  In code
		/// invoked a large number of times, the loop should be written directly into
		/// the code and not use this method.  Also, the Linq version performs even
		/// worse.
		/// </summary>
		[JsonIgnore]
		public IEnumerable<UhtProperty> Properties
		{
			get
			{
				foreach (UhtType Type in this.Children)
				{
					if (Type is UhtProperty Property)
					{
						yield return Property;
					}
				}
			}
		}

		/// <summary>
		/// Return a collection of children that are functions
		/// 
		/// NOTE: This method allocates memory to construct the enumerator.  In code
		/// invoked a large number of times, the loop should be written directly into
		/// the code and not use this method.  Also, the Linq version performs even
		/// worse.
		/// </summary>
		[JsonIgnore]
		public IEnumerable<UhtFunction> Functions
		{
			get
			{
				foreach (UhtType Type in this.Children)
				{
					if (Type is UhtFunction Function)
					{
						yield return Function;
					}
				}
			}
		}

		[JsonIgnore]
		public virtual string EngineNamePrefix { get => "F"; }

		/// <inheritdoc/>
		public override string EngineClassName { get => "Struct"; }

		public UhtStruct? Super = null;

		[JsonConverter(typeof(UhtNullableTypeListJsonConverter<UhtStruct>))]
		public List<UhtStruct>? Bases { get; set; } = null;

		[JsonIgnore]
		public UhtStruct? SuperStruct => Super;

		public UhtStruct(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		/// <summary>
		/// Test to see if the given struct is derived from the base structure
		/// </summary>
		/// <param name="SomeBase">Base structure.</param>
		/// <returns>True if the given structure is the specified base or derives from the base.  If the base is null, the false is returned.</returns>
		public bool IsChildOf(UhtStruct? Base)
		{
			if (Base == null)
			{
				return false;
			}
			for (UhtStruct? Current = this; Current != null; Current = Current.Super)
			{
				if (Current == Base)
				{
					return true;
				}
			}
			return false;
		}

		#region Resolution support
		protected override void ResolveSuper(UhtResolvePhase ResolvePhase)
		{
			if (this.Super != null)
			{
				this.Super.Resolve(ResolvePhase);
			}

			if (this.Bases != null)
			{
				foreach (UhtStruct Base in this.Bases)
				{
					Base.Resolve(ResolvePhase);
				}
			}

			base.ResolveSuper(ResolvePhase);
		}

		/// <summary>
		/// Check properties to see if any instances are referenced.
		/// This method does NOT cache the result.
		/// </summary>
		/// <param name="bDeepScan">If true, the ScanForInstancedReferenced method on the properties will also be called.</param>
		/// <returns></returns>
		public virtual bool ScanForInstancedReferenced(bool bDeepScan)
		{
			foreach (UhtType Type in this.Children)
			{
				if (Type is UhtProperty Property)
				{
					if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ContainsInstancedReference | EPropertyFlags.InstancedReference))
					{
						return true;
					}
					if (bDeepScan && Property.ScanForInstancedReferenced(bDeepScan))
					{
						return true;
					}
				}
			}
			return false;
		}
		#endregion

		#region Super and base binding helper methods
		public void BindAndResolveSuper(ref UhtToken SuperIdentifier, UhtFindOptions FindOptions)
		{
			if (SuperIdentifier)
			{
				this.Super = (UhtStruct?)this.FindType(FindOptions | UhtFindOptions.SourceName | UhtFindOptions.NoSelf, ref SuperIdentifier);
				if (this.Super == null)
				{
					throw new UhtException(this, $"Unable to find parent {this.EngineType.ShortLowercaseText()} type for '{this.SourceName}' named '{SuperIdentifier.Value}'");
				}
				this.HeaderFile.AddReferencedHeader(this.Super);
				this.MetaData.Parent = this.Super.MetaData;
				this.Super.Resolve(UhtResolvePhase.Bases);
			}
		}

		public void BindAndResolveBases(List<UhtToken[]>? BaseIdentifiers, UhtFindOptions FindOptions)
		{
			if (BaseIdentifiers != null)
			{
				foreach (UhtToken[] BaseIdentifier in BaseIdentifiers)
				{
					// We really only case about interfaces, but we can also handle structs
					UhtStruct? Base = (UhtStruct?)this.FindType(FindOptions | UhtFindOptions.Class | UhtFindOptions.ScriptStruct | UhtFindOptions.SourceName | UhtFindOptions.NoSelf, BaseIdentifier);
					if (Base != null)
					{
						if (this.Bases == null)
						{
							this.Bases = new List<UhtStruct>();
						}
						this.Bases.Add(Base);
						this.HeaderFile.AddReferencedHeader(Base);
						Base.Resolve(UhtResolvePhase.Bases);
					}
				}
			}
		}
		#endregion

		#region Validation support
		protected override UhtValidationOptions Validate(UhtValidationOptions Options)
		{
			Options = base.Validate(Options);
			ValidateSparseClassData();
			return Options;
		}

		private static bool CheckUIMinMaxRangeFromMetaData(UhtType Child)
		{
			string UIMin = Child.MetaData.GetValueOrDefault(UhtNames.UIMin);
			string UIMax = Child.MetaData.GetValueOrDefault(UhtNames.UIMax);
			if (UIMin.Length == 0 || UIMax.Length == 0)
			{
				return false;
			}

			// NOTE: Old UHT didn't handle parse errors
			double MinValue;
			if (!double.TryParse(UIMin, out MinValue))
			{
				MinValue = 0;
			}

			double MaxValue;
			if (!double.TryParse(UIMax, out MaxValue))
			{
				MaxValue = 0;
			}

			// NOTE: that we actually allow UIMin == UIMax to disable the range manually.
			return MinValue <= MaxValue;
		}

		protected override void ValidateDocumentationPolicy(UhtDocumentationPolicy Policy)
		{
			if (Policy.bClassOrStructCommentRequired)
			{
				string ClassTooltip = this.MetaData.GetValueOrDefault(UhtNames.ToolTip);
				if (ClassTooltip.Length == 0 || ClassTooltip.Equals(this.EngineName, StringComparison.OrdinalIgnoreCase))
				{
					this.LogError($"{this.EngineType.CapitalizedText()} '{this.SourceName}' does not provide a tooltip / comment (DocumentationPolicy).");
				}
			}

			if (Policy.bMemberToolTipsRequired)
			{
				Dictionary<string, UhtProperty> ToolTipToType = new Dictionary<string, UhtProperty>();
				foreach (UhtProperty Property in this.Properties)
				{
					string ToolTip = Property.GetToolTipText();
					if (ToolTip.Length == 0 || ToolTip == Property.GetDisplayNameText())
					{
						Property.LogError($"Property '{this.SourceName}::{Property.SourceName}' does not provide a tooltip / comment (DocumentationPolicy).");
						continue;
					}

					UhtProperty? Existing;
					if (ToolTipToType.TryGetValue(ToolTip, out Existing))
					{
						Property.LogError($"Property '{this.SourceName}::{Existing.SourceName}' and '{this.SourceName}::{Property.SourceName}' are using identical tooltips (DocumentationPolicy).");
					}
					else
					{
						ToolTipToType.Add(ToolTip, Property);
					}
				}
			}

			if (Policy.bFloatRangesRequired)
			{
				foreach (UhtType Child in this.Children)
				{
					if (Child is UhtDoubleProperty || Child is UhtFloatProperty)
					{
						if (!CheckUIMinMaxRangeFromMetaData(Child))
						{
							Child.LogError($"Property '{this.SourceName}::{Child.SourceName}' does not provide a valid UIMin / UIMax (DocumentationPolicy).");
						}
					}
				}
			}

			// also compare all tooltips to see if they are unique
			if (Policy.bFunctionToolTipsRequired)
			{
				if (this is UhtClass)
				{
					Dictionary<string, UhtType> ToolTipToType = new Dictionary<string, UhtType>();
					foreach (UhtType Child in this.Children)
					{
						if (Child is UhtFunction Function)
						{
							string ToolTip = Function.GetToolTipText();
							if (ToolTip.Length == 0)
							{
								// NOTE: This does not fire because it doesn't check to see if it matches the display name as above.
								Child.LogError($"Function '{this.SourceName}::{Function.SourceName}' does not provide a tooltip / comment (DocumentationPolicy).");
								continue;
							}

							UhtType? Existing;
							if (ToolTipToType.TryGetValue(ToolTip, out Existing))
							{
								Child.LogError($"Functions '{this.SourceName}::{Existing.SourceName}' and '{this.SourceName}::{Function.SourceName}' are using identical tooltips / comments (DocumentationPolicy).");
							}
							else
							{
								ToolTipToType.Add(ToolTip, Function);
							}
						}
					}
				}
			}
		}

		protected void ValidateSparseClassData()
		{
			// Fetch the data types
			string[]? SparseClassDataTypes = this.MetaData.GetStringArray(UhtNames.SparseClassDataTypes);
			if (SparseClassDataTypes == null)
			{
				return;
			}

			// Make sure we don't try to have sparse class data inside of a struct instead of a class
			UhtClass? Class = this as UhtClass;
			if (Class == null)
			{
				this.LogError($"{this.EngineType.CapitalizedText()} '{this.SourceName}' contains sparse class data but is not a class.");
				return;
			}

			// for now we only support one sparse class data structure per class
			if (SparseClassDataTypes.Length > 1)
			{
				this.LogError($"Class '{this.SourceName}' contains multiple sparse class data types");
				return;
			}
			if (SparseClassDataTypes.Length == 0)
			{
				this.LogError($"Class '{this.SourceName}' has sparse class metadata but does not specify a type");
				return;
			}

			foreach (string SparseClassDataTypeName in SparseClassDataTypes)
			{
				UhtScriptStruct? SparseScriptStruct = this.Session.FindType(null, UhtFindOptions.EngineName | UhtFindOptions.ScriptStruct, SparseClassDataTypeName) as UhtScriptStruct;

				// make sure the sparse class data struct actually exists
				if (SparseScriptStruct == null)
				{
					this.LogError($"Unable to find sparse data type '{SparseClassDataTypeName}' for class '{this.SourceName}'");
					continue;
				}

				// check the data struct for invalid properties
				foreach (UhtProperty Property in SparseScriptStruct.Properties)
				{
					if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintAssignable))
					{
						Property.LogError($"Sparse class data types can not contain blueprint assignable delegates. Type '{SparseScriptStruct.EngineName}' Delegate '{Property.SourceName}'");
					}

					// all sparse properties should have EditDefaultsOnly
					if (!Property.PropertyFlags.HasAllFlags(EPropertyFlags.Edit | EPropertyFlags.DisableEditOnInstance))
					{
						Property.LogError($"Sparse class data types must be VisibleDefaultsOnly or EditDefaultsOnly. Type '{SparseScriptStruct.EngineName}' Property '{Property.SourceName}'");
					}

					// no sparse properties should have BlueprintReadWrite
					if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintVisible) && !Property.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly))
					{
						Property.LogError($"Sparse class data types must not be BlueprintReadWrite. Type '{SparseScriptStruct.EngineName}' Property '{Property.SourceName}'");
					}
				}

				// if the class's parent has a sparse class data struct then the current class must also use the same struct or one that inherits from it
				UhtClass? SuperClass = Class.SuperClass;
				if (SuperClass != null)
				{
					string[]? SuperSparseClassDataTypes = SuperClass.MetaData.GetStringArray(UhtNames.SparseClassDataTypes);
					if (SuperSparseClassDataTypes != null)
					{
						foreach (string SuperSparseClassDataTypeName in SuperSparseClassDataTypes)
						{
							UhtScriptStruct? SuperSparseScriptStruct = this.Session.FindType(null, UhtFindOptions.EngineName | UhtFindOptions.ScriptStruct, SuperSparseClassDataTypeName) as UhtScriptStruct;
							if (SuperSparseScriptStruct != null)
							{
								if (!SparseScriptStruct.IsChildOf(SuperSparseScriptStruct))
								{
									this.LogError(
										$"Class '{this.SourceName}' is a child of '{SuperClass.SourceName}' but its sparse class data struct " +
										$"'{SparseScriptStruct.EngineName}', does not inherit from '{SuperSparseScriptStruct.EngineName}'.");
								}
							}
						}
					}
				}
			}
		}
		#endregion
	}
}
