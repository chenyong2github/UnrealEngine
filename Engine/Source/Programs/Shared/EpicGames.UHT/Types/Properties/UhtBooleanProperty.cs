// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents the FBoolProperty engine type
	/// </summary>
	[UhtEngineClass(Name = "BoolProperty", IsProperty = true)]
	public abstract class UhtBooleanProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "BoolProperty"; }

		/// <summary>
		/// If true, the boolean is a native bool and not a UBOOL
		/// </summary>
		protected abstract bool bIsNativeBool { get; }

		/// <summary>
		/// Construct a new boolean property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		protected UhtBooleanProperty(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument = false)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.UserFacing:
				case UhtPropertyTextType.Construction:
				case UhtPropertyTextType.FunctionThunkParameterArrayType:
				case UhtPropertyTextType.FunctionThunkReturn:
				case UhtPropertyTextType.RigVMTemplateArg:
				case UhtPropertyTextType.ExportMember:
				case UhtPropertyTextType.GetterSetterArg:
					Builder.Append(this.CppTypeText);
					break;

				case UhtPropertyTextType.Sparse:
				case UhtPropertyTextType.SparseShort:
				case UhtPropertyTextType.ClassFunction:
				case UhtPropertyTextType.EventFunction:
				case UhtPropertyTextType.InterfaceFunction:
				case UhtPropertyTextType.EventParameterMember:
				case UhtPropertyTextType.EventParameterFunctionMember:
					Builder.Append("bool");
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			Builder.AppendMetaDataDecl(this, Context, Name, NameSuffix, Tabs);
			if (!(this.Outer is UhtProperty))
			{
				Builder.AppendTabs(Tabs).Append("static void ").AppendNameDecl(Context, Name, NameSuffix).Append("_SetBit(void* Obj);\r\n");
			}
			Builder.AppendTabs(Tabs).Append("static const UECodeGen_Private::").Append("FBoolPropertyParams").Append(' ').AppendNameDecl(Context, Name, NameSuffix).Append(";\r\n");
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			Builder.AppendMetaDataDef(this, Context, Name, NameSuffix, Tabs);
			if (this.Outer == Context.OuterStruct)
			{
				Builder.AppendTabs(Tabs).Append("void ").AppendNameDef(Context, Name, NameSuffix).Append("_SetBit(void* Obj)\r\n");
				Builder.AppendTabs(Tabs).Append("{\r\n");
				Builder.AppendTabs(Tabs + 1).Append("((").Append(Context.OuterStructSourceName).Append("*)Obj)->").Append(this.SourceName).Append(" = 1;\r\n");
				Builder.AppendTabs(Tabs).Append("}\r\n");
			}

			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FBoolPropertyParams",
				bIsNativeBool ?
				"UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool" :
				"UECodeGen_Private::EPropertyGenFlags::Bool ", 
				false, false);

			Builder.Append("sizeof(").Append(this.CppTypeText).Append("), ");

			if (this.Outer == Context.OuterStruct)
			{
				Builder
					.Append("sizeof(").Append(Context.OuterStructSourceName).Append("), ")
					.Append("&").AppendNameDef(Context, Name, NameSuffix).Append("_SetBit, ");
			}
			else
			{
				Builder.Append("0, nullptr, ");
			}

			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFullDecl(StringBuilder Builder, UhtPropertyTextType TextType, bool bSkipParameterName = false)
		{
			AppendText(Builder, TextType);

			//@todo we currently can't have out bools.. so this isn't really necessary, but eventually out bools may be supported, so leave here for now
			if (TextType.IsParameter() && this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
			{
				Builder.Append('&');
			}

			Builder.Append(' ');

			if (!bSkipParameterName)
			{
				Builder.Append(this.SourceName);
			}

			if (this.ArrayDimensions != null)
			{
				Builder.Append('[').Append(this.ArrayDimensions).Append(']');
			}
			else if (TextType == UhtPropertyTextType.ExportMember && !(this is UhtBoolProperty))
			{
				Builder.Append(":1");
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("false");
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			UhtToken Identifier = DefaultValueReader.GetIdentifier();
			if (Identifier.IsValue("true") || Identifier.IsValue("false"))
			{
				InnerDefaultValue.Append(Identifier.Value.ToString());
				return true;
			}
			return false;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtBooleanProperty;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return CppTypeText;
		}
	}

	/// <summary>
	/// Native boolean type specialization
	/// </summary>
	[UnrealHeaderTool]
	public class UhtBoolProperty : UhtBooleanProperty
	{
		/// <inheritdoc/>
		protected override string CppTypeText { get => "bool"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "UBOOL"; }

		/// <inheritdoc/>
		protected override bool bIsNativeBool { get => true; }

		/// <summary>
		/// Construct a new boolean property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		public UhtBoolProperty(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "bool", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		private static UhtProperty? BoolProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			if (PropertySettings.bIsBitfield)
			{
				TokenReader.LogError("bool bitfields are not supported.");
				return null;
			}
			return new UhtBoolProperty(PropertySettings);
		}
		#endregion
	}

	/// <summary>
	/// uint8 bit type specialization
	/// </summary>
	public class UhtBool8Property : UhtBooleanProperty
	{
		/// <inheritdoc/>
		protected override string CppTypeText { get => "uint8"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "UBOOL8"; }

		/// <inheritdoc/>
		protected override bool bIsNativeBool { get => false; }

		/// <summary>
		/// Construct a new boolean property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		public UhtBool8Property(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn;
		}
	}

	/// <summary>
	/// uint16 bit type specialization
	/// </summary>
	public class UhtBool16Property : UhtBooleanProperty
	{
		/// <inheritdoc/>
		protected override string CppTypeText { get => "uint16"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "UBOOL16"; }

		/// <inheritdoc/>
		protected override bool bIsNativeBool { get => false; }

		/// <summary>
		/// Construct a new boolean property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		public UhtBool16Property(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
		}
	}

	/// <summary>
	/// uint32 bit type specialization
	/// </summary>
	public class UhtBool32Property : UhtBooleanProperty
	{
		/// <inheritdoc/>
		protected override string CppTypeText { get => "uint32"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "UBOOL32"; }

		/// <inheritdoc/>
		protected override bool bIsNativeBool { get => false; }

		/// <summary>
		/// Construct a new boolean property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		public UhtBool32Property(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
		}
	}

	/// <summary>
	/// uint64 bit type specialization
	/// </summary>
	public class UhtBool64Property : UhtBooleanProperty
	{
		/// <inheritdoc/>
		protected override string CppTypeText { get => "uint64"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "UBOOL64"; }

		/// <inheritdoc/>
		protected override bool bIsNativeBool { get => false; }

		/// <summary>
		/// Construct a new boolean property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		public UhtBool64Property(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
		}
	}
}
