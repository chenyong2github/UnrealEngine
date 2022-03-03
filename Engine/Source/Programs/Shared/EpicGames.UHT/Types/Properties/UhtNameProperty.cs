// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "NameProperty", IsProperty = true)]
	public class UhtNameProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "NameProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "FName"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "PROPERTY"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.EngineClass; }

		public UhtNameProperty(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("NAME_None");
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FNamePropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FNamePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Name");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			if (DefaultValueReader.TryOptional("NAME_None"))
			{
				InnerDefaultValue.Append("None");
			}
			else if (DefaultValueReader.TryOptional("FName"))
			{
				DefaultValueReader.Require('(');
				StringView Value = DefaultValueReader.GetWrappedConstString();
				DefaultValueReader.Require(')');
				InnerDefaultValue.Append(Value);
			}
			else
			{
				StringView Value = DefaultValueReader.GetWrappedConstString();
				InnerDefaultValue.Append(Value);
			}
			return true;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtNameProperty;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return this.CppTypeText;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "FName", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? NameProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			return new UhtNameProperty(PropertySettings);
		}
		#endregion
	}
}
