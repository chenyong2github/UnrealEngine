// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "Int16Property", IsProperty = true)]
	public class UhtInt16Property : UhtNumericProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "Int16Property"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "int16"; }

		public UhtInt16Property(UhtPropertySettings PropertySettings, UhtPropertyIntType IntType) : base(PropertySettings, IntType)
		{
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FInt16PropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FInt16PropertyParams", "UECodeGen_Private::EPropertyGenFlags::Int16");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			return false;
		}

		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtInt16Property;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "int16", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? Int16Property(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			return new UhtInt16Property(PropertySettings, UhtPropertyIntType.Sized);
		}
		#endregion
	}
}
