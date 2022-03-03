// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public static class UhtEnumSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.Enum, ValueType = UhtSpecifierValueType.Legacy)]
		private static void FlagsSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtEnum Enum = (UhtEnum)SpecifierContext.Scope.ScopeType;
			Enum.EnumFlags |= EEnumFlags.Flags;
		}
	}
}
