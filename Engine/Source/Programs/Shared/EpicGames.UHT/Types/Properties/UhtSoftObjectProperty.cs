// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "SoftObjectProperty", IsProperty = true)]
	public class UhtSoftObjectProperty : UhtObjectPropertyBase
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "SoftObjectProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "SoftObjectPtr"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "SOFTOBJECT"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		public UhtSoftObjectProperty(UhtPropertySettings PropertySettings, UhtClass PropertyClass)
			: base(PropertySettings, PropertyClass, null)
		{
			this.PropertyFlags |= EPropertyFlags.UObjectWrapper;
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.CanExposeOnSpawn |
				UhtPropertyCaps.CanHaveConfig | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		protected UhtSoftObjectProperty(UhtPropertySettings PropertySettings, UhtClass PropertyClass, UhtClass MetaClass)
			: base(PropertySettings, PropertyClass, MetaClass)
		{
			this.PropertyFlags |= EPropertyFlags.UObjectWrapper;
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.CanExposeOnSpawn |
				UhtPropertyCaps.CanHaveConfig | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("NULL");
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				default:
					Builder.Append("TSoftObjectPtr<").Append(this.Class.SourceName).Append(">");
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FSoftObjectPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FSoftObjectPropertyParams", "UECodeGen_Private::EPropertyGenFlags::SoftObject");
			AppendMemberDefRef(Builder, Context, this.Class, false);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtSoftObjectProperty OtherObject)
			{
				return this.Class == OtherObject.Class && this.MetaClass == OtherObject.MetaClass;
			}
			return false;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return $"TSoftObjectPtr<{this.Class.SourceName}>";
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TSoftObjectPtr")]
		public static UhtProperty? SoftObjectPtrProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtClass? PropertyClass = UhtObjectPropertyBase.ParseTemplateObject(PropertySettings, TokenReader, MatchedToken, true);
			if (PropertyClass == null)
			{
				return null;
			}

			if (PropertyClass.IsChildOf(PropertyClass.Session.UClass))
			{
				TokenReader.LogError("Class variables cannot be stored in TSoftObjectPtr, use TSoftClassPtr instead.");
			}

			return new UhtSoftObjectProperty(PropertySettings, PropertyClass);
		}
		#endregion
	}
}
