// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public static class UhtPropertyMemberSpecifiers
	{
		public static UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.PropertyMember);
		public static UhtSpecifierValidatorTable SpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.PropertyMember);

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void EditAnywhereSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenEditSpecifier)
			{
				Context.MessageSite.LogError("Found more than one edit/visibility specifier (EditAnywhere), only one is allowed");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Edit;
			Context.bSeenEditSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void EditInstanceOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenEditSpecifier)
			{
				Context.MessageSite.LogError("Found more than one edit/visibility specifier (EditInstanceOnly), only one is allowed");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Edit | EPropertyFlags.DisableEditOnTemplate;
			Context.bSeenEditSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void EditDefaultsOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenEditSpecifier)
			{
				Context.MessageSite.LogError("Found more than one edit/visibility specifier (EditDefaultsOnly), only one is allowed");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Edit | EPropertyFlags.DisableEditOnInstance;
			Context.bSeenEditSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void VisibleAnywhereSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenEditSpecifier)
			{
				Context.MessageSite.LogError("Found more than one edit/visibility specifier (VisibleAnywhere), only one is allowed");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Edit | EPropertyFlags.EditConst;
			Context.bSeenEditSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void VisibleInstanceOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenEditSpecifier)
			{
				Context.MessageSite.LogError("Found more than one edit/visibility specifier (VisibleInstanceOnly), only one is allowed");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Edit | EPropertyFlags.EditConst | EPropertyFlags.DisableEditOnTemplate;
			Context.bSeenEditSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void VisibleDefaultsOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenEditSpecifier)
			{
				Context.MessageSite.LogError("Found more than one edit/visibility specifier (VisibleDefaultsOnly), only one is allowed");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Edit | EPropertyFlags.EditConst | EPropertyFlags.DisableEditOnInstance;
			Context.bSeenEditSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintReadWriteSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenBlueprintReadOnlySpecifier)
			{
				Context.MessageSite.LogError("Cannot specify a property as being both BlueprintReadOnly and BlueprintReadWrite.");
			}

			string? PrivateAccessMD;
			bool bAllowPrivateAccess = Context.MetaData.TryGetValue(UhtNames.AllowPrivateAccess, out PrivateAccessMD) && !PrivateAccessMD.Equals("false", StringComparison.OrdinalIgnoreCase);
			if (SpecifierContext.Scope.AccessSpecifier == UhtAccessSpecifier.Private && !bAllowPrivateAccess)
			{
				Context.MessageSite.LogError("BlueprintReadWrite should not be used on private members");
			}

			if (Context.PropertySettings.PropertyFlags.HasAnyFlags(EPropertyFlags.EditorOnly) && Context.PropertySettings.Outer is UhtScriptStruct)
			{
				Context.MessageSite.LogError("Blueprint exposed struct members cannot be editor only");
			}

			Context.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintVisible;
			Context.bSeenBlueprintWriteSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintReadOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenBlueprintWriteSpecifier)
			{
				Context.MessageSite.LogError("Cannot specify both BlueprintReadOnly and BlueprintReadWrite or BlueprintSetter.");
			}

			string? PrivateAccessMD;
			bool bAllowPrivateAccess = Context.MetaData.TryGetValue(UhtNames.AllowPrivateAccess, out PrivateAccessMD) && !PrivateAccessMD.Equals("false", StringComparison.OrdinalIgnoreCase);
			if (SpecifierContext.Scope.AccessSpecifier == UhtAccessSpecifier.Private && !bAllowPrivateAccess)
			{
				Context.MessageSite.LogError("BlueprintReadOnly should not be used on private members");
			}

			if (Context.PropertySettings.PropertyFlags.HasAnyFlags(EPropertyFlags.EditorOnly) && Context.PropertySettings.Outer is UhtScriptStruct)
			{
				Context.MessageSite.LogError("Blueprint exposed struct members cannot be editor only");
			}

			Context.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintVisible | EPropertyFlags.BlueprintReadOnly;
			Context.bSeenBlueprintReadOnlySpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.SingleString)]
		private static void BlueprintSetterSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.bSeenBlueprintReadOnlySpecifier)
			{
				Context.MessageSite.LogError("Cannot specify a property as being both BlueprintReadOnly and having a BlueprintSetter.");
			}

			if (Context.PropertySettings.Outer is UhtScriptStruct)
			{
				Context.MessageSite.LogError("Cannot specify BlueprintSetter for a struct member.");
			}

			Context.MetaData.Add(UhtNames.BlueprintSetter, Value.ToString());

			Context.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintVisible;
			Context.bSeenBlueprintWriteSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.SingleString)]
		private static void BlueprintGetterSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.PropertySettings.Outer is UhtScriptStruct)
			{
				Context.MessageSite.LogError("Cannot specify BlueprintGetter for a struct member.");
			}

			Context.MetaData.Add(UhtNames.BlueprintGetter, Value.ToString());

			Context.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintVisible;
			Context.bSeenBlueprintGetterSpecifier = true;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ConfigSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Config;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void GlobalConfigSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.GlobalConfig | EPropertyFlags.Config;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void LocalizedSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.MessageSite.LogError("The Localized specifier is deprecated");
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void TransientSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Transient;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void DuplicateTransientSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.DuplicateTransient;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void TextExportTransientSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.TextExportTransient;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NonPIETransientSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.MessageSite.LogWarning("NonPIETransient is deprecated - NonPIEDuplicateTransient should be used instead");
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.NonPIEDuplicateTransient;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NonPIEDuplicateTransientSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.NonPIEDuplicateTransient;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ExportSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.ExportObject;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void EditInlineSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.MessageSite.LogError("EditInline is deprecated. Remove it, or use Instanced instead.");
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NoClearSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.NoClear;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void EditFixedSizeSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.EditFixedSize;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ReplicatedSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.PropertySettings.Outer is UhtScriptStruct)
			{
				Context.MessageSite.LogError("Struct members cannot be replicated");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Net;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.SingleString)]
		private static void ReplicatedUsingSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.PropertySettings.Outer is UhtScriptStruct)
			{
				Context.MessageSite.LogError("Struct members cannot be replicated");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Net;

			if (Value.Span.Length == 0)
			{
				Context.MessageSite.LogError("Must specify a valid function name for replication notifications");
			}
			Context.PropertySettings.RepNotifyName = Value.ToString();
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.RepNotify;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NotReplicatedSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (!(Context.PropertySettings.Outer is UhtScriptStruct))
			{
				Context.MessageSite.LogError("Only Struct members can be marked NotReplicated");
			}
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.RepSkip;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void RepRetrySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.MessageSite.LogError("'RepRetry' is deprecated.");
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void InterpSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.Edit | EPropertyFlags.BlueprintVisible | EPropertyFlags.Interp;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NonTransactionalSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.NonTransactional;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void InstancedSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.PersistentInstance | EPropertyFlags.ExportObject | EPropertyFlags.InstancedReference;
			Context.MetaData.Add(UhtNames.EditInline, true);
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintAssignableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintAssignable;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintCallableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintCallable;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintAuthorityOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.BlueprintAuthorityOnly;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void AssetRegistrySearchableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.AssetRegistrySearchable;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void SimpleDisplaySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.SimpleDisplay;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void AdvancedDisplaySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.AdvancedDisplay;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void SaveGameSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.SaveGame;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.Legacy)]
		private static void SkipSerializationSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.SkipSerialization;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void GetterSpecifier(UhtSpecifierContext SpecifierContext, StringView? Value)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (!(Context.PropertySettings.Outer is UhtClass))
			{
				Context.MessageSite.LogError("Only class members can have Setters");
			}
			Context.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.GetterSpecified;
			if (Value != null)
			{
				StringView Temp = (StringView)Value;
				if (Temp.Length > 0)
				{
					if (Temp.Equals("None", StringComparison.OrdinalIgnoreCase))
					{
						Context.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.GetterSpecifiedNone;
					}
					else
					{
						Context.PropertySettings.Getter = Value.ToString();
					}
				}
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyMember, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void SetterSpecifier(UhtSpecifierContext SpecifierContext, StringView? Value)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (!(Context.PropertySettings.Outer is UhtClass))
			{
				Context.MessageSite.LogError("Only class members can have Getters");
			}
			Context.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.SetterSpecified;
			if (Value != null)
			{
				StringView Temp = (StringView)Value;
				if (Temp.Length > 0)
				{
					if (Temp.Equals("None", StringComparison.OrdinalIgnoreCase))
					{
						Context.PropertySettings.PropertyExportFlags |= UhtPropertyExportFlags.SetterSpecifiedNone;
					}
					else
					{
						Context.PropertySettings.Setter = Value.ToString();
					}
				}
			}
		}
	}
}
