// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "LazyObjectProperty", IsProperty = true)]
	public class UhtLazyObjectPtrProperty : UhtObjectPropertyBase
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "LazyObjectProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "TLazyObjectPtr"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "LAZYOBJECT"; }

		/// <inheritdoc/>
		protected override bool bPGetPassAsNoPtr { get => true; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		public UhtLazyObjectPtrProperty(UhtPropertySettings PropertySettings, UhtClass PropertyClass)
			: base(PropertySettings, PropertyClass, null)
		{
			this.PropertyFlags |= EPropertyFlags.UObjectWrapper;
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.RequiresNullConstructorArg;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.Append("TLazyObjectPtr<").Append(this.Class.SourceName).Append("> "); //COMPATIBILITY-TODO - Extra space
					break;

				default:
					Builder.Append("TLazyObjectPtr<").Append(this.Class.SourceName).Append(">");
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FLazyObjectPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FLazyObjectPropertyParams", "UECodeGen_Private::EPropertyGenFlags::LazyObject");
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
			if (Other is UhtLazyObjectPtrProperty OtherObject)
			{
				return this.Class == OtherObject.Class && this.MetaClass == OtherObject.MetaClass;
			}
			return false;
		}

		/// <inheritdoc/>
		public override void Validate(UhtStruct OuterStruct, UhtProperty OutermostProperty, UhtValidationOptions Options)
		{
			base.Validate(OuterStruct, OutermostProperty, Options);

			// UFunctions with a smart pointer as input parameter wont compile anyway, because of missing P_GET_... macro.
			// UFunctions with a smart pointer as return type will crash when called via blueprint, because they are not supported in VM.
			if (this.PropertyCategory != UhtPropertyCategory.Member)
			{
				OuterStruct.LogError("UFunctions cannot take a lazy pointer as a parameter.");
			}
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return $"TLazyObjectPtr<{this.Class.SourceName}>";
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TLazyObjectPtr")]
		public static UhtProperty? LazyObjectPtrProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtClass? PropertyClass = ParseTemplateObject(PropertySettings, TokenReader, MatchedToken, true);
			if (PropertyClass == null)
			{
				return null;
			}

			if (PropertyClass.IsChildOf(PropertyClass.Session.UClass))
			{
				TokenReader.LogError("Class variables cannot be lazy, they are always strong.");
			}

			return new UhtLazyObjectPtrProperty(PropertySettings, PropertyClass);
		}
		#endregion
	}
}
