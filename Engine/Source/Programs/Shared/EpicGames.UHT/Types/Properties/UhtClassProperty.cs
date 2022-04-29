// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// FClassProperty
	/// </summary>
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

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		/// <param name="Class">Referenced class</param>
		/// <param name="MetaClass">Reference meta class</param>
		/// <param name="ExtraFlags">Extra flags to apply to the property.</param>
		public UhtClassProperty(UhtPropertySettings PropertySettings, UhtClass Class, UhtClass MetaClass, EPropertyFlags ExtraFlags = EPropertyFlags.None)
			: base(PropertySettings, Class, MetaClass)
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
				Builder.Append(this.Class.SourceName).Append('*');
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.Generic:
				case UhtPropertyTextType.Sparse:
				case UhtPropertyTextType.SparseShort:
				case UhtPropertyTextType.GenericFunctionArgOrRetVal:
				case UhtPropertyTextType.GenericFunctionArgOrRetValImpl:
				case UhtPropertyTextType.ClassFunctionArgOrRetVal:
				case UhtPropertyTextType.EventFunctionArgOrRetVal:
				case UhtPropertyTextType.InterfaceFunctionArgOrRetVal:
				case UhtPropertyTextType.ExportMember:
				case UhtPropertyTextType.Construction:
				case UhtPropertyTextType.FunctionThunkParameterArrayType:
				case UhtPropertyTextType.RigVMType:
				case UhtPropertyTextType.GetterSetterArg:
					AppendText(Builder);
					break;

				case UhtPropertyTextType.FunctionThunkRetVal:
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

		#region Keyword
		[UhtPropertyType(Keyword = "TSubclassOf")]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtProperty? SubclassOfProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
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
