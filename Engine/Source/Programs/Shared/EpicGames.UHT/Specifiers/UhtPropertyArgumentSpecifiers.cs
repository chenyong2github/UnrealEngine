// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics.CodeAnalysis;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// Collection of property argument specifiers
	/// </summary>
	[UnrealHeaderTool]
	public static class UhtPropertyArgumentSpecifiers
	{
		#region Argument Property Specifiers
		[UhtSpecifier(Extends = UhtTableNames.PropertyArgument, ValueType = UhtSpecifierValueType.Legacy)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		private static void ConstSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.ConstParm;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyArgument, ValueType = UhtSpecifierValueType.Legacy)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		private static void RefSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtPropertySpecifierContext Context = (UhtPropertySpecifierContext)SpecifierContext;
			Context.PropertySettings.PropertyFlags |= EPropertyFlags.OutParm | EPropertyFlags.ReferenceParm;
		}

		[UhtSpecifier(Extends = UhtTableNames.PropertyArgument, ValueType = UhtSpecifierValueType.Legacy)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
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
