// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics.CodeAnalysis;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// Collection of UENUM specifiers
	/// </summary>
	[UnrealHeaderTool]
	public static class UhtEnumSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.Enum, ValueType = UhtSpecifierValueType.Legacy)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		private static void FlagsSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtEnum Enum = (UhtEnum)SpecifierContext.Scope.ScopeType;
			Enum.EnumFlags |= EEnumFlags.Flags;
		}
	}
}
