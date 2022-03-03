// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "LargeWorldCoordinatesRealProperty", IsProperty = true)]
	public class UhtLargeWorldCoordinatesRealProperty : UhtNumericProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "LargeWorldCoordinatesRealProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "double"; }

		public UhtLargeWorldCoordinatesRealProperty(UhtPropertySettings PropertySettings) : base(PropertySettings, UhtPropertyIntType.None)
		{
			this.PropertyCaps |= UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FLargeWorldCoordinatesRealPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, 
				"FLargeWorldCoordinatesRealPropertyParams", 
				"UECodeGen_Private::EPropertyGenFlags::LargeWorldCoordinatesReal");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			InnerDefaultValue.AppendFormat("{0:F6}", DefaultValueReader.GetConstFloatExpression());
			return true;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtLargeWorldCoordinatesRealProperty;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "FLargeWorldCoordinatesReal", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? LargeWorldCoordinatesRealProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			if (!PropertySettings.Outer.HeaderFile.bIsNoExportTypes)
			{
				TokenReader.LogError("FLargeWorldCoordinatesReal is intended for LWC support only and should not be used outside of NoExportTypes.h");
			}
			return new UhtLargeWorldCoordinatesRealProperty(PropertySettings);
		}
		#endregion
	}
}
