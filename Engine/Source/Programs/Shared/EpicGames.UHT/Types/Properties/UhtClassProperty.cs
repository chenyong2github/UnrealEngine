// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "ClassProperty", IsProperty = true)]
	public class UhtClassProperty : UhtObjectProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "ClassProperty"; }

		/// <inheritdoc/>
		protected override bool bPGetPassAsNoPtr { get => this.PropertyFlags.HasAnyFlags(EPropertyFlags.UObjectWrapper); }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		public UhtClassProperty(UhtPropertySettings PropertySettings, UhtClass PropertyClass, UhtClass MetaClass, EPropertyFlags ExtraFlags = EPropertyFlags.None)
			: base(PropertySettings, PropertyClass, MetaClass)
		{
			this.PropertyFlags |= ExtraFlags;
			this.PropertyCaps |= UhtPropertyCaps.CanHaveConfig;
			this.PropertyCaps &= ~(UhtPropertyCaps.CanBeInstanced);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return this.MetaClass != null ? $"class {this.MetaClass.SourceName};" : null;
		}

		/// <inheritdoc/>
		private StringBuilder AppendSubClassText(StringBuilder Builder)
		{
			Builder.Append("TSubclassOf<").Append(this.MetaClass?.SourceName).Append("> "); //COMPATIBILITY-TODO - Extra space in old UHT
			return Builder;
		}

		/// <inheritdoc/>
		private StringBuilder AppendText(StringBuilder Builder)
		{
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.UObjectWrapper))
			{
				AppendSubClassText(Builder);
			}
			else
			{
				Builder.Append(this.Class.SourceName).Append("*");
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.UserFacing:
				case UhtPropertyTextType.Sparse:
				case UhtPropertyTextType.SparseShort:
				case UhtPropertyTextType.ClassFunction:
				case UhtPropertyTextType.EventFunction:
				case UhtPropertyTextType.InterfaceFunction:
				case UhtPropertyTextType.ExportMember:
				case UhtPropertyTextType.Construction:
				case UhtPropertyTextType.FunctionThunkParameterArrayType:
				case UhtPropertyTextType.RigVMTemplateArg:
				case UhtPropertyTextType.GetterSetterArg:
					AppendText(Builder);
					break;

				case UhtPropertyTextType.FunctionThunkReturn:
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
					{
						Builder.Append("const ");
					}
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.UObjectWrapper))
					{
						AppendText(Builder);
					}
					else
					{
						Builder.Append("UClass*");
					}
					break;

				case UhtPropertyTextType.EventParameterMember:
				case UhtPropertyTextType.EventParameterFunctionMember:
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm) && !this.PropertyFlags.HasAnyFlags(EPropertyFlags.UObjectWrapper))
					{
						Builder.Append("UClass*");
					}
					else
					{
						AppendText(Builder);
					}
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					if (this.PropertyFlags.HasAllFlags(EPropertyFlags.OutParm | EPropertyFlags.UObjectWrapper))
					{
						AppendSubClassText(Builder);
					}
					else
					{
						Builder.Append(this.Class.SourceName);
					}
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return base.AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FClassPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FClassPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Class");
			AppendMemberDefRef(Builder, Context, this.Class, false);
			AppendMemberDefRef(Builder, Context, this.MetaClass, false);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.UObjectWrapper))
			{
				return $"TSubclassOf<{this.MetaClass?.SourceName}> "; //COMPATIBILITY-TODO - Extra space in old UHT
			}
			else
			{
				return $"{this.Class}*";
			}
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TSubclassOf")]
		public static UhtProperty? SubclassOfProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtClass? MetaClass = UhtObjectPropertyBase.ParseTemplateClass(PropertySettings, TokenReader, MatchedToken);
			if (MetaClass == null)
			{
				return null;
			}

			// With TSubclassOf, MetaClass is used as a class limiter.  
			return new UhtClassProperty(PropertySettings, MetaClass.Session.UClass, MetaClass, EPropertyFlags.UObjectWrapper);
		}
		#endregion
	}
}
