// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "ByteProperty", IsProperty = true)]
	public class UhtByteProperty : UhtNumericProperty
	{
		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtEnum>))]
		public UhtEnum? Enum { get; set; }

		/// <inheritdoc/>
		public override string EngineClassName { get => "ByteProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "uint8"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => this.Enum == null || this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "PROPERTY" : "ENUM"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => this.Enum == null || this.Enum.CppForm != UhtEnumCppForm.EnumClass ? UhtPGetArgumentType.EngineClass : UhtPGetArgumentType.TypeText; }

		public UhtByteProperty(UhtPropertySettings PropertySettings, UhtPropertyIntType IntType, UhtEnum? Enum = null) : base(PropertySettings, IntType)
		{
			this.Enum = Enum;
			this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			if (this.Enum != null)
			{
				yield return this.Enum;
			}
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			Collector.AddCrossModuleReference(this.Enum, true);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			if (this.Enum != null)
			{
				return UhtEnumProperty.AppendEnumText(Builder, this, this.Enum, TextType, bIsTemplateArgument);
			}
			else
			{
				return base.AppendText(Builder, TextType, bIsTemplateArgument);
			}
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FBytePropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FBytePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Byte");
			AppendMemberDefRef(Builder, Context, this.Enum, true, true);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
			Builder.AppendObjectHash(StartingLength, this, Context, this.Enum);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterArg(StringBuilder Builder)
		{
			if (this.Enum != null)
			{
				return UhtEnumProperty.AppendEnumFunctionThunkParameterArg(Builder, this, this.Enum);
			}
			else
			{
				return base.AppendFunctionThunkParameterArg(Builder);
			}
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			if (this.Enum != null)
			{
				return UhtEnumProperty.SanitizeEnumDefaultValue(this, this.Enum, DefaultValueReader, InnerDefaultValue);
			}

			int Value = DefaultValueReader.GetConstIntExpression();
			InnerDefaultValue.Append(Value.ToString());
			return Value >= byte.MinValue && Value <= byte.MaxValue;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtByteProperty OtherByte)
			{
				return this.Enum == OtherByte.Enum;
			}
			else if (Other is UhtEnumProperty OtherEnum)
			{
				return this.Enum == OtherEnum.Enum;
			}
			return false;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction Function, UhtValidationOptions Options)
		{
			base.ValidateFunctionArgument(Function, Options);

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.BlueprintCallable))
			{
				if (this.Enum != null && this.Enum.UnderlyingType != UhtEnumUnderlyingType.uint8 && this.Enum.UnderlyingType != UhtEnumUnderlyingType.Unspecified)
				{
					this.LogError("Invalid enum param for Blueprints - currently only uint8 supported");
				}
			}
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			if (this.Enum != null)
			{
				return UhtEnumProperty.GetEnumRigVMType(this, this.Enum, ref ParameterFlags);
			}
			else
			{
				return CppTypeText;
			}
		}

		#region Keyword
		[UhtPropertyType(Keyword = "uint8", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		private static UhtProperty? UInt8Property(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			if (PropertySettings.bIsBitfield)
			{
				return new UhtBool8Property(PropertySettings);
			}
			else
			{
				return new UhtByteProperty(PropertySettings, UhtPropertyIntType.Sized);
			}
		}
		#endregion
	}
}
