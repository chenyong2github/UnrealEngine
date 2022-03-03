// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public static class UhtDefaultSpecifiers
	{
		private static void SetMetaData(UhtSpecifierContext SpecifierContext, StringView Key, StringView Value)
		{
			if (Key.Length == 0)
			{
				SpecifierContext.MessageSite.LogError("Invalid metadata, name can not be blank");
				return;
			}

			// Trim the leading and ending whitespace
			ReadOnlySpan<char> Span = Value.Span;
			int Start = 0;
			int End = Value.Length;
			for (; Start < End; ++Start)
			{
				if (!UhtFCString.IsWhitespace(Span[Start]))
				{
					break;
				}
			}
			for (; Start < End; --End)
			{
				if (!UhtFCString.IsWhitespace(Span[End - 1]))
				{
					break;
				}
			}

			// Trim any quotes 
			//COMPATIBILITY-TODO - This doesn't handle strings that end in an escaped ".  This is a bug in old UHT.
			// Only remove quotes if we have quotes at both ends and the last isn't escaped.
			if (Start < End && Span[Start] == '"')
			{
				++Start;
			}
			if (Start < End && Span[End - 1] == '"')
			{
				--End;
			}

			// Get the trimmed string
			Value = new StringView(Value, Start, End - Start);

			// Make sure this isn't a duplicate assignment
			string KeyAsString = Key.ToString();
			if (SpecifierContext.MetaData.TryGetValue(KeyAsString, out string? ExistingValue))
			{
				if (StringViewComparer.OrdinalIgnoreCase.Compare(ExistingValue, Value) != 0)
				{
					SpecifierContext.MessageSite.LogError($"Metadata key '{Key}' first seen with value '{ExistingValue}' then '{Value}'");
				}
			}
			else
			{
				SpecifierContext.MetaData.Add(KeyAsString, SpecifierContext.MetaNameIndex, Value.ToString());
			}
		}

		private static void SetMetaData(UhtSpecifierContext SpecifierContext, StringView Key, bool Value)
		{
			SetMetaData(SpecifierContext, Key, Value ? "true" : "false");
		}

		#region Specifiers
		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.KeyValuePairList, When = UhtSpecifierWhen.Immediate)]
		private static void MetaSpecifier(UhtSpecifierContext SpecifierContext, List<KeyValuePair<StringView, StringView>> Value)
		{
			foreach (KeyValuePair<StringView, StringView> KVP in (List<KeyValuePair<StringView, StringView>>)Value)
			{
				SetMetaData(SpecifierContext, KVP.Key, KVP.Value);
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.String, When = UhtSpecifierWhen.Immediate)]
		private static void DisplayNameSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			SetMetaData(SpecifierContext, "DisplayName", Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.String, When = UhtSpecifierWhen.Immediate)]
		private static void FriendlyNameSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			SetMetaData(SpecifierContext, "FriendlyName", Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void BlueprintInternalUseOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "BlueprintInternalUseOnly", true);
			SetMetaData(SpecifierContext, "BlueprintType", true);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void BlueprintInternalUseOnlyHierarchicalSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "BlueprintInternalUseOnlyHierarchical", true);
			SetMetaData(SpecifierContext, "BlueprintInternalUseOnly", true);
			SetMetaData(SpecifierContext, "BlueprintType", true);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void BlueprintTypeSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "BlueprintType", true);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void NotBlueprintTypeSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "NotBlueprintType", true);
			SpecifierContext.MetaData.Remove("BlueprintType", SpecifierContext.MetaNameIndex);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void BlueprintableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "IsBlueprintBase", true);
			SetMetaData(SpecifierContext, "BlueprintType", true);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void CallInEditorSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "CallInEditor", true);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void NotBlueprintableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "IsBlueprintBase", false);
			SpecifierContext.MetaData.Remove("BlueprintType", SpecifierContext.MetaNameIndex);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.String, When = UhtSpecifierWhen.Immediate)]
		private static void CategorySpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			SetMetaData(SpecifierContext, "Category", Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void ExperimentalSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "DevelopmentStatus", "Experimental");
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void EarlyAccessPreviewSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, "DevelopmentStatus", "EarlyAccess");
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.None, When = UhtSpecifierWhen.Immediate)]
		private static void DocumentationPolicySpecifier(UhtSpecifierContext SpecifierContext)
		{
			SetMetaData(SpecifierContext, UhtNames.DocumentationPolicy, "Strict");
		}

		[UhtSpecifier(Extends = UhtTableNames.Default, ValueType = UhtSpecifierValueType.String, When = UhtSpecifierWhen.Immediate)]
		private static void SparseClassDataTypeSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			SetMetaData(SpecifierContext, "SparseClassDataType", Value);
		}
		#endregion

		#region Validators
		[UhtSpecifierValidator(Name = "UIMin", Extends = UhtTableNames.Default)]
		[UhtSpecifierValidator(Name = "UIMax", Extends = UhtTableNames.Default)]
		[UhtSpecifierValidator(Name = "ClampMin", Extends = UhtTableNames.Default)]
		[UhtSpecifierValidator(Name = "ClampMax", Extends = UhtTableNames.Default)]
		private static void ValidateNumeric(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value)
		{
			if (!UhtFCString.IsNumeric(Value.Span))
			{
				Type.LogError($"Metadata value for '{Key}' is non-numeric : '{Value}'");
			}
		}

		[UhtSpecifierValidator(Extends = UhtTableNames.Default)]
		private static void DevelopmentStatusSpecifierValidator(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value)
		{
			string[] AllowedValues = { "EarlyAccess", "Experimental" };
			foreach (string AllowedValue in AllowedValues)
			{
				if (Value.Equals(AllowedValue, StringComparison.OrdinalIgnoreCase))
				{
					return;
				}
			}
			Type.LogError($"'{Key.Name}' metadata was '{Value}' but it must be {UhtUtilities.MergeTypeNames(AllowedValues, "or", false)}");
		}

		[UhtSpecifierValidator(Extends = UhtTableNames.Default)]
		private static void DocumentationPolicySpecifierValidator(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value)
		{
			const string StrictValue = "Strict";
			if (!Value.Span.Equals(StrictValue, StringComparison.OrdinalIgnoreCase))
			{
				Type.LogError(MetaData.LineNumber, $"'{Key}' metadata was '{Value}' but it must be '{StrictValue}'");
			}
		}

		[UhtSpecifierValidator(Extends = UhtTableNames.Default)]
		private static void UnitsSpecifierValidator(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value)
		{
			// Check for numeric property
			if (Type is UhtProperty)
			{
				if (!(Type is UhtNumericProperty) && !(Type is UhtStructProperty))
				{
					Type.LogError("'Units' meta data can only be applied to numeric and struct properties");
				}
			}

			if (!UhtConfig.Instance.IsValidUnits(Value))
			{
				Type.LogError($"Unrecognized units '{Value}' specified for '{Type.FullName}'");
			}
		}
		#endregion
	}
}
