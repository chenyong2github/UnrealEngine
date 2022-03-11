// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using System.Text;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// FMulticastInlineDelegateProperty
	/// </summary>
	[UhtEngineClass(Name = "MulticastInlineDelegateProperty", IsProperty = true)]
	public class UhtMulticastInlineDelegateProperty : UhtMulticastDelegateProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "MulticastInlineDelegateProperty"; }

		/// <summary>
		/// Construct new property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		/// <param name="Function">Referenced function</param>
		public UhtMulticastInlineDelegateProperty(UhtPropertySettings PropertySettings, UhtFunction Function) : base(PropertySettings, Function)
		{
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.Append("FMulticastInlineDelegateProperty");
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
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FMulticastDelegatePropertyParams", "UECodeGen_Private::EPropertyGenFlags::InlineMulticastDelegate");
			AppendMemberDefRef(Builder, Context, this.Function, true);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}
	}
}
