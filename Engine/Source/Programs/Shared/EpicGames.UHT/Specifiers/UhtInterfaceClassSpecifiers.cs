// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	class UhtInterfaceClassSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.Interface, ValueType = UhtSpecifierValueType.Legacy)]
		private static void DependsOnSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClass Class = (UhtClass)SpecifierContext.Scope.ScopeType;
			throw new UhtException(SpecifierContext.MessageSite, $"The dependsOn specifier is deprecated. Please use #include \"ClassHeaderFilename.h\" instead.");
		}

		[UhtSpecifier(Extends = UhtTableNames.Interface, ValueType = UhtSpecifierValueType.Legacy)]
		private static void MinimalAPISpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClass Class = (UhtClass)SpecifierContext.Scope.ScopeType;
			Class.ClassFlags |= EClassFlags.MinimalAPI;
		}

		[UhtSpecifier(Extends = UhtTableNames.Interface, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ConversionRootSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtClass Class = (UhtClass)SpecifierContext.Scope.ScopeType;
			Class.MetaData.Add(UhtNames.IsConversionRoot, "true");
		}
	}
}
