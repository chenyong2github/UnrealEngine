// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Text.Json.Serialization;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// FByteProperty
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "ByteProperty", IsProperty = true)]
	public class UhtByteProperty : UhtNumericProperty
	{
		/// <summary>
		/// Referenced enumeration (TEnumAsByte)
		/// </summary>
		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtEnum>))]
		public UhtEnum? Enum { get; set; }

		/// <inheritdoc/>
		public override string EngineClassName => "ByteProperty";

		/// <inheritdoc/>
		protected override string CppTypeText => "uint8";

		/// <inheritdoc/>
		protected override string PGetMacroText => this.Enum == null || this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "PROPERTY" : "ENUM";

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument => this.Enum == null || this.Enum.CppForm != UhtEnumCppForm.EnumClass ? UhtPGetArgumentType.EngineClass : UhtPGetArgumentType.TypeText;

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="propertySettings">Property settings</param>
		/// <param name="intType">Integer type</param>
		/// <param name="enumObj">Optional referenced enum</param>
		public UhtByteProperty(UhtPropertySettings propertySettings, UhtPropertyIntType intType, UhtEnum? enumObj = null) : base(propertySettings, intType)
		{
			this.Enum = enumObj;
			this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint |
				UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.SupportsRigVM;
			if (this.Enum != null)
			{
				this.PropertyCaps |= UhtPropertyCaps.IsRigVMEnum;
			}
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
		public override void CollectReferencesInternal(IUhtReferenceCollector collector, bool templateProperty)
		{
			collector.AddCrossModuleReference(this.Enum, true);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder builder, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			if (this.Enum != null)
			{
				return UhtEnumProperty.AppendEnumText(builder, this, this.Enum, textType, isTemplateArgument);
			}
			else
			{
				return base.AppendText(builder, textType, isTemplateArgument);
			}
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			return AppendMemberDecl(builder, context, name, nameSuffix, tabs, "FBytePropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, string? offset, int tabs)
		{
			AppendMemberDefStart(builder, context, name, nameSuffix, offset, tabs, "FBytePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Byte");
			AppendMemberDefRef(builder, context, this.Enum, true, true);
			AppendMemberDefEnd(builder, context, name, nameSuffix);
			return builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder builder, int startingLength, IUhtPropertyMemberContext context)
		{
			builder.AppendObjectHash(startingLength, this, context, this.Enum);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterArg(StringBuilder builder)
		{
			if (this.Enum != null)
			{
				return UhtEnumProperty.AppendEnumFunctionThunkParameterArg(builder, this, this.Enum);
			}
			else
			{
				return base.AppendFunctionThunkParameterArg(builder);
			}
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader defaultValueReader, StringBuilder innerDefaultValue)
		{
			if (this.Enum != null)
			{
				return UhtEnumProperty.SanitizeEnumDefaultValue(this, this.Enum, defaultValueReader, innerDefaultValue);
			}

			int value = defaultValueReader.GetConstIntExpression();
			innerDefaultValue.Append(value);
			return value >= Byte.MinValue && value <= Byte.MaxValue;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty other)
		{
			if (other is UhtByteProperty otherByte)
			{
				return this.Enum == otherByte.Enum;
			}
			else if (other is UhtEnumProperty otherEnum)
			{
				return this.Enum == otherEnum.Enum;
			}
			return false;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction function, UhtValidationOptions options)
		{
			base.ValidateFunctionArgument(function, options);

			if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.BlueprintCallable))
			{
				if (this.Enum != null && this.Enum.UnderlyingType != UhtEnumUnderlyingType.uint8 && this.Enum.UnderlyingType != UhtEnumUnderlyingType.Unspecified)
				{
					this.LogError("Invalid enum param for Blueprints - currently only uint8 supported");
				}
			}
		}

		#region Keyword
		[UhtPropertyType(Keyword = "uint8", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtProperty? UInt8Property(UhtPropertyResolvePhase resolvePhase, UhtPropertySettings propertySettings, IUhtTokenReader tokenReader, UhtToken matchedToken)
		{
			if (propertySettings.IsBitfield)
			{
				return new UhtBoolProperty(propertySettings, UhtBoolType.UInt8);
			}
			else
			{
				return new UhtByteProperty(propertySettings, UhtPropertyIntType.Sized);
			}
		}
		#endregion
	}
}
