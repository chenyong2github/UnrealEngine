// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "WeakObjectProperty", IsProperty = true)]
	public class UhtWeakObjectPtrProperty : UhtObjectPropertyBase
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "WeakObjectProperty"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => this.PropertyFlags.HasAnyFlags(EPropertyFlags.AutoWeak) ? "AUTOWEAKOBJECT" : "WEAKOBJECT"; }

		/// <inheritdoc/>
		protected override bool bPGetPassAsNoPtr { get => true; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		public UhtWeakObjectPtrProperty(UhtPropertySettings PropertySettings, UhtClass PropertyClass, EPropertyFlags ExtraFlags = EPropertyFlags.None)
			: base(PropertySettings, PropertyClass, null)
		{
			this.PropertyFlags |= EPropertyFlags.UObjectWrapper | ExtraFlags;
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.IsMemberSupportedByBlueprint;
			this.PropertyCaps &= ~(UhtPropertyCaps.IsParameterSupportedByBlueprint);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				default:
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.AutoWeak))
					{
						Builder.Append("TAutoWeakObjectPtr<").Append(this.Class.SourceName).Append(">");
					}
					else
					{
						Builder.Append("TWeakObjectPtr<").Append(this.Class.SourceName).Append(">");
					}
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FWeakObjectPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FWeakObjectPropertyParams", "UECodeGen_Private::EPropertyGenFlags::WeakObject");
			AppendMemberDefRef(Builder, Context, this.Class, false);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("NULL");
			return Builder;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtWeakObjectPtrProperty OtherObject)
			{
				return this.Class == OtherObject.Class && this.MetaClass == OtherObject.MetaClass;
			}
			return false;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.AutoWeak))
			{
				return $"TAutoWeakObjectPtr<{this.Class.SourceName}>";
			}
			else
			{
				return $"TWeakObjectPtr<{this.Class.SourceName}>";
			}
		}

		#region Keywords
		protected static UhtProperty CreateWeakProperty(UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtClass PropertyClass, EPropertyFlags ExtraFlags = EPropertyFlags.None)
		{
			if (PropertyClass.IsChildOf(PropertyClass.Session.UClass))
			{
				TokenReader.LogError("Class variables cannot be weak, they are always strong.");
			}

			if (PropertySettings.DisallowPropertyFlags.HasAnyFlags(EPropertyFlags.AutoWeak))
			{
				return new UhtObjectProperty(PropertySettings, PropertyClass, ExtraFlags | EPropertyFlags.UObjectWrapper);
			}
			return new UhtWeakObjectPtrProperty(PropertySettings, PropertyClass, ExtraFlags);
		}

		[UhtPropertyType(Keyword = "TWeakObjectPtr")]
		public static UhtProperty? WeakObjectProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtClass? PropertyClass = ParseTemplateObject(PropertySettings, TokenReader, MatchedToken, true);
			if (PropertyClass == null)
			{
				return null;
			}

			return CreateWeakProperty(PropertySettings, TokenReader, PropertyClass);
		}

		[UhtPropertyType(Keyword = "TAutoWeakObjectPtr")]
		public static UhtProperty? AutoWeakObjectProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtClass? PropertyClass = ParseTemplateObject(PropertySettings, TokenReader, MatchedToken, true);
			if (PropertyClass == null)
			{
				return null;
			}

			return CreateWeakProperty(PropertySettings, TokenReader, PropertyClass, EPropertyFlags.AutoWeak);
		}
		#endregion
	}
}
