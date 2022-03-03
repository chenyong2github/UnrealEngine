// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public static class UhtScriptStructSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NoExportSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)SpecifierContext.Scope.ScopeType;
			ScriptStruct.ScriptStructFlags |= EStructFlags.NoExport;
			ScriptStruct.ScriptStructFlags &= ~EStructFlags.Native;
		}

		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, ValueType = UhtSpecifierValueType.Legacy)]
		private static void AtomicSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)SpecifierContext.Scope.ScopeType;
			ScriptStruct.ScriptStructFlags |= EStructFlags.Atomic;
		}

		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ImmutableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)SpecifierContext.Scope.ScopeType;
			ScriptStruct.ScriptStructFlags |= EStructFlags.Atomic | EStructFlags.Immutable;
		}

		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, ValueType = UhtSpecifierValueType.Legacy)]
		private static void HasDefaultsSpecifier(UhtSpecifierContext SpecifierContext)
		{
			if (!SpecifierContext.Scope.HeaderParser.HeaderFile.bIsNoExportTypes)
			{
				SpecifierContext.MessageSite.LogError("The 'HasDefaults' struct specifier is only valid in the NoExportTypes.h file");
			}
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)SpecifierContext.Scope.ScopeType;
			ScriptStruct.ScriptStructExportFlags |= UhtScriptStructExportFlags.HasDefaults;
		}

		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, ValueType = UhtSpecifierValueType.Legacy)]
		private static void HasNoOpConstructorSpecifier(UhtSpecifierContext SpecifierContext)
		{
			if (!SpecifierContext.Scope.HeaderParser.HeaderFile.bIsNoExportTypes)
			{
				SpecifierContext.MessageSite.LogError("The 'HasNoOpConstructor' struct specifier is only valid in the NoExportTypes.h file");
			}
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)SpecifierContext.Scope.ScopeType;
			ScriptStruct.ScriptStructExportFlags |= UhtScriptStructExportFlags.HasNoOpConstructor;
		}

		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, ValueType = UhtSpecifierValueType.Legacy)]
		private static void IsAlwaysAccessibleSpecifier(UhtSpecifierContext SpecifierContext)
		{
			if (!SpecifierContext.Scope.HeaderParser.HeaderFile.bIsNoExportTypes)
			{
				SpecifierContext.MessageSite.LogError("The 'IsAlwaysAccessible' struct specifier is only valid in the NoExportTypes.h file");
			}
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)SpecifierContext.Scope.ScopeType;
			ScriptStruct.ScriptStructExportFlags |= UhtScriptStructExportFlags.IsAlwaysAccessible;
		}

		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, ValueType = UhtSpecifierValueType.Legacy)]
		private static void IsCoreTypeSpecifier(UhtSpecifierContext SpecifierContext)
		{
			if (!SpecifierContext.Scope.HeaderParser.HeaderFile.bIsNoExportTypes)
			{
				SpecifierContext.MessageSite.LogError("The 'IsCoreType' struct specifier is only valid in the NoExportTypes.h file");
			}
			UhtScriptStruct ScriptStruct = (UhtScriptStruct)SpecifierContext.Scope.ScopeType;
			ScriptStruct.ScriptStructExportFlags |= UhtScriptStructExportFlags.IsCoreType;
		}
	}
}
