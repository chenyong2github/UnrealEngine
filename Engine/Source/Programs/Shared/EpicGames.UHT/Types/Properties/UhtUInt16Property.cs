// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "UInt16Property", IsProperty = true)]
	public class UhtUInt16Property : UhtNumericProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "UInt16Property"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "uint16"; }

		public UhtUInt16Property(UhtPropertySettings PropertySettings, UhtPropertyIntType IntType) : base(PropertySettings, IntType)
		{
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FFInt16PropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FFInt16PropertyParams", "UECodeGen_Private::EPropertyGenFlags::UInt16");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			return false;
		}

		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtUInt16Property;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "uint16", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? UInt16Property(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			if (PropertySettings.bIsBitfield)
			{
				return new UhtBool8Property(PropertySettings);
			}
			else
			{
				return new UhtUInt16Property(PropertySettings, UhtPropertyIntType.Sized);
			}
		}
		#endregion
	}
}
