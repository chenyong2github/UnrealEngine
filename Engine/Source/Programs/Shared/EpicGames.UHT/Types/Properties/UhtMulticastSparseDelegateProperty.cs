// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UhtEngineClass(Name = "MulticastSparseDelegateProperty", IsProperty = true)]
	public class UhtMulticastSparseDelegateProperty : UhtMulticastDelegateProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "MulticastSparseDelegateProperty"; }

		public UhtMulticastSparseDelegateProperty(UhtPropertySettings PropertySettings, UhtFunction Function) : base(PropertySettings, Function)
		{
		}

		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.Append("FMulticastSparseDelegateProperty");
					break;

				default:
					base.AppendText(Builder, TextType, bIsTemplateArgument);
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FMulticastDelegatePropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FMulticastDelegatePropertyParams", "UECodeGen_Private::EPropertyGenFlags::SparseMulticastDelegate");
			AppendMemberDefRef(Builder, Context, this.Function, true);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}
	}
}
