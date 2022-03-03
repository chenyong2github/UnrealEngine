// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public static class UhtPropertyArgumentSpecifiers
	{
		public static readonly UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.PropertyArgument);
		public static readonly UhtSpecifierValidatorTable SpecifierValidatorTable = UhtSpecifierValidatorTables.Instance.Get(UhtTableNames.PropertyArgument);

		#region Argument Property Specifiers
		[UhtSpecifier(Extends = UhtTableNames.PropertyArgument, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ConstSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.ConstParm;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyArgument, ValueType = UhtSpecifierValueType.Legacy)]
		private static void RefSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.OutParm | EPropertyFlags.ReferenceParm;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyArgument, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NotReplicatedSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			if (Context.PropertySettings.PropertyCategory == UhtPropertyCategory.ReplicatedParameter)
			{
				Context.PropertySettings.PropertyCategory = UhtPropertyCategory.RegularParameter;
				Context.PropertySettings.PropertyFlags |= EPropertyFlags.RepSkip;
			}
			else
			{
				Context.MessageSite.LogError("Only parameters in service request functions can be marked NotReplicated");
			}
		}
		#endregion
	}
}
