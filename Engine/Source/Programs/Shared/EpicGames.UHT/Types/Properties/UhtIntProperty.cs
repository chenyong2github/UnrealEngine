// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "IntProperty", IsProperty = true)]
	public class UhtIntProperty : UhtNumericProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "IntProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "int32"; }

		public UhtIntProperty(UhtPropertySettings PropertySettings, UhtPropertyIntType IntType) : base(PropertySettings, IntType)
		{
			this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, 
				this.IntType == UhtPropertyIntType.Unsized ? "FUnsizedIntPropertyParams" : "FIntPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs,
				this.IntType == UhtPropertyIntType.Unsized ? "FUnsizedIntPropertyParams" : "FIntPropertyParams",
				"UECodeGen_Private::EPropertyGenFlags::Int");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			InnerDefaultValue.Append(DefaultValueReader.GetConstIntExpression().ToString());
			return true;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtIntProperty;
		}

		#region Keywords
		[UhtPropertyType(Keyword = "int32", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? Int32Property(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			return new UhtIntProperty(PropertySettings, UhtPropertyIntType.Sized);
		}

		[UhtPropertyType(Keyword = "int", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? IntProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			return new UhtIntProperty(PropertySettings, UhtPropertyIntType.Unsized);
		}

		[UhtPropertyType(Keyword = "signed", Options = UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? SignedProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			TokenReader
				.Require("signed")
				.Optional("int");
			return new UhtIntProperty(PropertySettings, UhtPropertyIntType.Unsized);
		}
		#endregion
	}
}
