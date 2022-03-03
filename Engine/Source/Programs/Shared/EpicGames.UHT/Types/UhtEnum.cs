// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	public enum UhtEnumCppForm
	{
		Regular,
		Namespaced,
		EnumClass
	}

	public enum UhtEnumUnderlyingType
	{
		Unspecified,
		uint8,
		uint16,
		uint32,
		uint64,
		int8,
		int16,
		int32,
		int64
	}

	public struct UhtEnumValue
	{
		public string Name { get; internal set; }
		public long Value { get; internal set; }
	}

	[UhtEngineClass(Name = "Enum")]
	public class UhtEnum : UhtField, IUhtMetaDataKeyConversion
	{
		private static UhtSpecifierValidatorTable EnumSpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.Enum);

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EEnumFlags EnumFlags { get; internal set; } = EEnumFlags.None;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtEnumCppForm CppForm { get; internal set; } = UhtEnumCppForm.Regular;

		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtEnumUnderlyingType UnderlyingType { get; internal set; } = UhtEnumUnderlyingType.uint8;

		public string CppType { get; internal set; } = String.Empty;
		public bool bIsEditorOnly { get; internal set; } = false;
		public List<UhtEnumValue> EnumValues { get; set; }

		[JsonIgnore]
		public override UhtEngineType EngineType => UhtEngineType.Enum;

		/// <inheritdoc/>
		public override string EngineClassName { get => "Enum"; }

		///<inheritdoc/>
		[JsonIgnore]
		protected override UhtSpecifierValidatorTable? SpecifierValidatorTable { get => UhtEnum.EnumSpecifierValidatorTable; }

		public UhtEnum(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
			this.MetaData.KeyConversion = this;
			this.EnumValues = new List<UhtEnumValue>();
		}

		public bool IsValidEnumValue(long Value)
		{
			foreach (UhtEnumValue EnumValue in EnumValues)
			{
				if (EnumValue.Value == Value)
				{
					return true;
				}
			}
			return false;
		}

		public int GetIndexByName(string Name)
		{
			Name = CleanEnumValueName(Name);
			for (int Index = 0; Index < EnumValues.Count; ++Index)
			{
				if (this.EnumValues[Index].Name == Name)
				{
					return Index;
				}
			}
			return -1;
		}

		public string GetMetaDataKey(string Name, int NameIndex)
		{
			string EnumName = this.EnumValues[NameIndex].Name;
			if (this.CppForm != UhtEnumCppForm.Regular)
			{
				int ScopeIndex = EnumName.IndexOf("::");
				if (ScopeIndex >= 0)
				{
					EnumName = EnumName.Substring(ScopeIndex + 2);
				}
			}
			return $"{EnumName}.{Name}";
		}

		public string GetFullEnumName(string ShortEnumName)
		{
			switch (this.CppForm)
			{
				case UhtEnumCppForm.Namespaced:
				case UhtEnumCppForm.EnumClass:
					return $"{this.SourceName}::{ShortEnumName}";

				case UhtEnumCppForm.Regular:
					return ShortEnumName;

				default:
					throw new UhtIceException("Unexpected EEnumCppForm value");
			}
		}

		public int AddEnumValue(string ShortEnumName, long Value)
		{
			int EnumIndex = this.EnumValues.Count;
			this.EnumValues.Add(new UhtEnumValue { Name = GetFullEnumName(ShortEnumName), Value = Value });
			return EnumIndex;
		}

		private string CleanEnumValueName(string Name)
		{
			int LastColons = Name.LastIndexOf("::");
			return LastColons == -1 ? GetFullEnumName(Name) : GetFullEnumName(Name.Substring(LastColons + 2));		
		}

		#region Validation support
		protected override void ValidateDocumentationPolicy(UhtDocumentationPolicy Policy)
		{
			if (Policy.bClassOrStructCommentRequired)
			{
				if (!this.MetaData.ContainsKey(UhtNames.ToolTip))
				{
					this.LogError(this.MetaData.LineNumber, $"Enum '{this.SourceName}' does not provide a tooltip / comment (DocumentationPolicy)");
				}
			}

			Dictionary<string, string> ToolTips = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			for (int EnumIndex = 0; EnumIndex < this.EnumValues.Count; ++EnumIndex)
			{
				string? EntryName;
				if (!this.MetaData.TryGetValue(UhtNames.Name, EnumIndex, out EntryName))
				{
					continue;
				}

				string? ToolTip;
				if (!this.MetaData.TryGetValue(UhtNames.ToolTip, EnumIndex, out ToolTip))
				{
					this.LogError(this.MetaData.LineNumber, $"Enum entry '{this.SourceName}::{EntryName}' does not provide a tooltip / comment (DocumentationPolicy)");
					continue;
				}

				string? DupName;
				if (ToolTips.TryGetValue(ToolTip, out DupName))
				{
					this.LogError(this.MetaData.LineNumber, $"Enum entries '{this.SourceName}::{EntryName}' and '{this.SourceName}::{DupName}' have identical tooltips / comments (DocumentationPolicy)");
				}
				else
				{
					ToolTips.Add(ToolTip, EntryName);
				}
			}
		}
		#endregion

		/// <inheritdoc/>
		public override void CollectReferences(IUhtReferenceCollector Collector)
		{
			Collector.AddExportType(this);
			Collector.AddDeclaration(this, true);
			Collector.AddCrossModuleReference(this, true);
			Collector.AddCrossModuleReference(this.Package, true);
		}
	}
}
