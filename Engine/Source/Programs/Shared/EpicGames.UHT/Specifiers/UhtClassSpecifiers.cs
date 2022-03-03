// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public static class UhtClassSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NoExportSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.NoExport);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void IntrinsicSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Intrinsic);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ComponentWrapperClassSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SpecifierContext.MetaData.Add(UhtNames.IgnoreCategoryKeywordsInSubclasses, true);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.SingleString)]
		private static void WithinSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.ClassWithinIdentifier = Value.ToString();
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void EditInlineNewSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.EditInlineNew);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NotEditInlineNewSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.RemoveClassFlags(EClassFlags.EditInlineNew);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void PlaceableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.RemoveClassFlags(EClassFlags.NotPlaceable);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NotPlaceableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.NotPlaceable);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void DefaultToInstancedSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.DefaultToInstanced);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void HideDropdownSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.HideDropDown);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void HiddenSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// Prevents class from appearing in the editor class browser and edit inline menus.
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Hidden);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void DependsOnSpecifier(UhtSpecifierContext SpecifierContext)
		{
			SpecifierContext.MessageSite.LogError("The dependsOn specifier is deprecated. Please use #include \"ClassHeaderFilename.h\" instead.");
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void MinimalAPISpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.MinimalAPI);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ConstSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Const);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void PerObjectConfigSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.PerObjectConfig);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ConfigDoNotCheckDefaultsSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.ConfigDoNotCheckDefaults);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void AbstractSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Abstract);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void DeprecatedSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Deprecated | EClassFlags.NotPlaceable);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void TransientSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Transient);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NonTransientSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.RemoveClassFlags(EClassFlags.Transient);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void OptionalSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// Optional class
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Optional);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void CustomConstructorSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// we will not export a constructor for this class, assuming it is in the CPP block
			UhtClass Class = (UhtClass)SpecifierContext.Scope.ScopeType;
			Class.ClassExportFlags |= UhtClassExportFlags.HasCustomConstructor;
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.SingleString)]
		private static void ConfigSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			UhtClass Class = (UhtClass)SpecifierContext.Scope.ScopeType;
			Class.Config = Value.ToString();
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void DefaultConfigSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// Save object config only to Default INIs, never to local INIs.
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.DefaultConfig);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void GlobalUserConfigSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// Save object config only to global user overrides, never to local INIs
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.GlobalUserConfig);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ProjectUserConfigSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// Save object config only to project user overrides, never to INIs that are checked in
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.ProjectUserConfig);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.SingleString)]
		private static void EditorConfigSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			SpecifierContext.MetaData.Add(UhtNames.EditorConfig, Value.ToString());
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void ShowCategoriesSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.ShowCategories.AddUniqueRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void HideCategoriesSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.HideCategories.AddUniqueRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void ShowFunctionsSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.ShowFunctions.AddUniqueRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void HideFunctionsSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.HideFunctions.AddUniqueRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.SingleString)]
		private static void SparseClassDataTypesSpecifier(UhtSpecifierContext SpecifierContext, StringView Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.SparseClassDataTypes.AddUnique(Value.ToString());
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void ClassGroupSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			foreach (StringView Eleemnt in Value)
			{
				Class.ClassGroupNames.Add(Eleemnt.ToString());
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void AutoExpandCategoriesSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AutoExpandCategories.AddUniqueRange(Value);
			Class.AutoCollapseCategories.RemoveSwapRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void AutoCollapseCategoriesSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AutoCollapseCategories.AddUniqueRange(Value);
			Class.AutoExpandCategories.RemoveSwapRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void PrioritizeCategoriesSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.PrioritizeCategories.AddUniqueRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.NonEmptyStringList)]
		private static void DontAutoCollapseCategoriesSpecifier(UhtSpecifierContext SpecifierContext, List<StringView> Value)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AutoCollapseCategories.RemoveSwapRange(Value);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void CollapseCategoriesSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// Class' properties should not be shown categorized in the editor.
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.CollapseCategories);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void DontCollapseCategoriesSpecifier(UhtSpecifierContext SpecifierContext)
		{
			// Class' properties should be shown categorized in the editor.
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.RemoveClassFlags(EClassFlags.CollapseCategories);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void AdvancedClassDisplaySpecifier(UhtSpecifierContext SpecifierContext)
		{
			// By default the class properties are shown in advanced sections in UI
			UhtClass Class = (UhtClass)SpecifierContext.Scope.ScopeType;
			Class.MetaData.Add(UhtNames.AdvancedClassDisplay, "true");
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ConversionRootSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClass Class = (UhtClass)SpecifierContext.Scope.ScopeType;
			Class.MetaData.Add(UhtNames.IsConversionRoot, "true");
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NeedsDeferredDependencyLoadingSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.NeedsDeferredDependencyLoading);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void MatchedSerializersSpecifier(UhtSpecifierContext SpecifierContext)
		{
			if (!SpecifierContext.Scope.HeaderParser.HeaderFile.bIsNoExportTypes)
			{
				SpecifierContext.MessageSite.LogError("The 'MatchedSerializers' class specifier is only valid in the NoExportTypes.h file");
			}
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.MatchedSerializers);
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.Legacy)]
		private static void InterfaceSpecifier(UhtSpecifierContext SpecifierContext)
		{
			if (!SpecifierContext.Scope.HeaderParser.HeaderFile.bIsNoExportTypes)
			{
				SpecifierContext.MessageSite.LogError("The 'Interface' class specifier is only valid in the NoExportTypes.h file");
			}
			UhtClassParser Class = (UhtClassParser)SpecifierContext.Scope.ScopeType;
			Class.AddClassFlags(EClassFlags.Interface);
		}
	}
}
