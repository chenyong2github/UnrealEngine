// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// FDoubleProperty
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "DoubleProperty", IsProperty = true)]
	public class UhtDoubleProperty : UhtNumericProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "DoubleProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "double"; }

		/// <summary>
		/// Create new property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		public UhtDoubleProperty(UhtPropertySettings PropertySettings) : base(PropertySettings, UhtPropertyIntType.None)
		{
			this.PropertyCaps |= UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FDoublePropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FDoublePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Double");
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
			return Other is UhtDoubleProperty;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "double", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtProperty? DoubleProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			return new UhtDoubleProperty(PropertySettings);
		}
		#endregion
	}
}
