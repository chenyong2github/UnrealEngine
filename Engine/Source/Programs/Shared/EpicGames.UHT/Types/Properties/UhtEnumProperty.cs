// Copyright Epic Games, Inc. All Rights Reserved.

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
	/// FEnumProperty
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "EnumProperty", IsProperty = true)]
	public class UhtEnumProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName => this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "ByteProperty" : "EnumProperty";

		/// <inheritdoc/>
		protected override string CppTypeText => "invalid";

		/// <inheritdoc/>
		protected override string PGetMacroText => this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "PROPERTY" : "ENUM";

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument => this.Enum.CppForm != UhtEnumCppForm.EnumClass ? UhtPGetArgumentType.EngineClass : UhtPGetArgumentType.TypeText;

		/// <summary>
		/// Referenced enum
		/// </summary>
		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtEnum>))]
		public UhtEnum Enum { get; set; }

		/// <summary>
		/// Underlying property set when the enum has an underlying interger type
		/// </summary>
		public UhtProperty? UnderlyingProperty { get; set; }

		/// <summary>
		/// Underlying type which defaults to int32 if the referenced enum doesn't have an underlying type
		/// </summary>
		public UhtEnumUnderlyingType UnderlyingType => this.Enum.UnderlyingType != UhtEnumUnderlyingType.Unspecified ? this.Enum.UnderlyingType : UhtEnumUnderlyingType.int32;

		/// <summary>
		/// Underlying type size.  Defaults to unsized if the referenced enum doesn't have an underlying type
		/// </summary>
		public UhtPropertyIntType UnderlyingTypeSize => this.Enum.UnderlyingType != UhtEnumUnderlyingType.Unspecified ? UhtPropertyIntType.Sized : UhtPropertyIntType.Unsized;

		/// <summary>
		/// Construct property
		/// </summary>
		/// <param name="propertySettings">Property settings</param>
		/// <param name="enumObj">Referenced enum</param>
		public UhtEnumProperty(UhtPropertySettings propertySettings, UhtEnum enumObj) : base(propertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint |
				UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.SupportsRigVM | UhtPropertyCaps.IsRigVMEnum;
			this.Enum = enumObj;
			this.HeaderFile.AddReferencedHeader(enumObj);
			if (this.Enum.CppForm == UhtEnumCppForm.EnumClass)
			{
				this.UnderlyingProperty = CreateUnderlyingProperty();
			}
			else
			{
				this.UnderlyingProperty = null;
			}
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector collector, bool templateProperty)
		{
			collector.AddCrossModuleReference(this.Enum, true);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			if (this.Enum.CppForm != UhtEnumCppForm.EnumClass)
			{
				return null;
			}
			return $"enum class {this.Enum.SourceName} : {this.UnderlyingType};";
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			yield return this.Enum;
		}

		/// <summary>
		/// Append enum text
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="property">Property in question</param>
		/// <param name="enumObj">Referenced enum</param>
		/// <param name="textType">Type of text to append</param>
		/// <param name="isTemplateArgument">If true, this property is a template argument</param>
		/// <returns></returns>
		public static StringBuilder AppendEnumText(StringBuilder builder, UhtProperty property, UhtEnum enumObj, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			switch (textType)
			{
				case UhtPropertyTextType.GenericFunctionArgOrRetVal:
				case UhtPropertyTextType.GenericFunctionArgOrRetValImpl:
				case UhtPropertyTextType.ClassFunctionArgOrRetVal:
				case UhtPropertyTextType.InterfaceFunctionArgOrRetVal:
				case UhtPropertyTextType.EventFunctionArgOrRetVal:
					if (enumObj.CppForm == UhtEnumCppForm.EnumClass || 
						(!isTemplateArgument && (property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm) || 
						!property.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))))
					{
						builder.Append(enumObj.CppType);
					}
					else
					{
						builder.Append("TEnumAsByte<").Append(enumObj.CppType).Append('>');
					}
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					builder.Append(enumObj.SourceName);
					break;

				case UhtPropertyTextType.RigVMType:
					if (enumObj.CppForm == UhtEnumCppForm.EnumClass || !isTemplateArgument)
					{
						builder.Append(enumObj.CppType);
					}
					else
					{
						builder.Append("TEnumAsByte<").Append(enumObj.CppType).Append('>');
					}
					break;

				case UhtPropertyTextType.GetterSetterArg:
					builder.Append(enumObj.CppType);
					break;

				default:
					if (enumObj.CppForm == UhtEnumCppForm.EnumClass)
					{
						builder.Append(enumObj.CppType);
					}
					else
					{
						builder.Append("TEnumAsByte<").Append(enumObj.CppType).Append('>');
					}
					break;
			}
			return builder;
		}

		/// <summary>
		/// Append the text for a function thunk call argument
		/// </summary>
		/// <param name="builder">Output builder</param>
		/// <param name="property">Property in question</param>
		/// <param name="enumObj">Referenced enum</param>
		/// <returns>Output builder</returns>
		public static StringBuilder AppendEnumFunctionThunkParameterArg(StringBuilder builder, UhtProperty property, UhtEnum enumObj)
		{
			if (!property.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
			{
				builder.Append(enumObj.CppType).Append('(').AppendFunctionThunkParameterName(property).Append(')');
			}
			else if (enumObj.CppForm == UhtEnumCppForm.EnumClass)
			{
				builder.Append('(').Append(enumObj.CppType).Append("&)(").AppendFunctionThunkParameterName(property).Append(')');
			}
			else
			{
				builder.Append("(TEnumAsByte<").Append(enumObj.CppType).Append(">&)(").AppendFunctionThunkParameterName(property).Append(')');
			}
			return builder;
		}

		/// <summary>
		/// Sanitize the default value for an enumeration
		/// </summary>
		/// <param name="property">Property in question</param>
		/// <param name="enumObj">Referenced enumeration</param>
		/// <param name="defaultValueReader">Default value</param>
		/// <param name="innerDefaultValue">Destination builder</param>
		/// <returns>True if the default value was parsed.</returns>
		public static bool SanitizeEnumDefaultValue(UhtProperty property, UhtEnum enumObj, IUhtTokenReader defaultValueReader, StringBuilder innerDefaultValue)
		{
			UhtTokenList cppIdentifier = defaultValueReader.GetCppIdentifier(UhtCppIdentifierOptions.None);
			UhtTokenList startingPoint = cppIdentifier.Next ?? cppIdentifier;
			startingPoint.Join(innerDefaultValue, "::");
			UhtTokenListCache.Return(cppIdentifier);

			int entryIndex = enumObj.GetIndexByName(innerDefaultValue.ToString());
			if (entryIndex == -1)
			{
				return false;
			}
			if (enumObj.MetaData.ContainsKey(UhtNames.Hidden, entryIndex))
			{
				property.LogError($"Hidden enum entries cannot be used as default values: '{property.SourceName}' '{innerDefaultValue}'");
			}
			return true;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder builder, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			return AppendEnumText(builder, this, this.Enum, textType, isTemplateArgument);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			if (this.UnderlyingProperty != null)
			{
				builder.AppendMemberDecl(this.UnderlyingProperty, context, name, GetNameSuffix(nameSuffix, "_Underlying"), tabs);
			}
			return AppendMemberDecl(builder, context, name, nameSuffix, tabs, this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "FBytePropertyParams" : "FEnumPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, string? offset, int tabs)
		{
			if (this.Enum.CppForm == UhtEnumCppForm.EnumClass)
			{
				if (this.UnderlyingProperty != null)
				{
					builder.AppendMemberDef(this.UnderlyingProperty, context, name, GetNameSuffix(nameSuffix, "_Underlying"), "0", tabs);
				}
				AppendMemberDefStart(builder, context, name, nameSuffix, offset, tabs, "FEnumPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Enum");
				AppendMemberDefRef(builder, context, this.Enum, true);
				AppendMemberDefEnd(builder, context, name, nameSuffix);
				return builder;
			}
			else
			{
				AppendMemberDefStart(builder, context, name, nameSuffix, offset, tabs, "FBytePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Byte", true, true);
				AppendMemberDefRef(builder, context, this.Enum, true);
				AppendMemberDefEnd(builder, context, name, nameSuffix);
			}
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberPtr(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			if (this.UnderlyingProperty != null)
			{
				builder.AppendMemberPtr(this.UnderlyingProperty, context, name, GetNameSuffix(nameSuffix, "_Underlying"), tabs);
			}
			base.AppendMemberPtr(builder, context, name, nameSuffix, tabs);
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterArg(StringBuilder builder)
		{
			return AppendEnumFunctionThunkParameterArg(builder, this, this.Enum);
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder builder, int startingLength, IUhtPropertyMemberContext context)
		{
			builder.AppendObjectHash(startingLength, this, context, this.Enum);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder builder, bool isInitializer)
		{
			if (this.Enum.CppForm != UhtEnumCppForm.EnumClass)
			{
				builder.Append('0');
			}
			else
			{
				builder.Append('(').Append(this.Enum.CppType).Append(")0");
			}
			return builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader defaultValueReader, StringBuilder innerDefaultValue)
		{
			return SanitizeEnumDefaultValue(this, this.Enum, defaultValueReader, innerDefaultValue);
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty other)
		{
			if (other is UhtEnumProperty otherEnum)
			{
				return this.Enum == otherEnum.Enum;
			}
			else if (other is UhtByteProperty otherByte)
			{
				return this.Enum == otherByte.Enum;
			}
			return false;
		}

		private UhtProperty CreateUnderlyingProperty()
		{
			UhtPropertySettings propertySettings = new UhtPropertySettings();
			propertySettings.Reset(this, 0, this.PropertyCategory, 0);
			propertySettings.SourceName = "UnderlyingType";
			switch (this.UnderlyingType)
			{
				case UhtEnumUnderlyingType.int8:
					return new UhtInt8Property(propertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.int16:
					return new UhtInt16Property(propertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.int32:
					return new UhtIntProperty(propertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.int64:
					return new UhtInt64Property(propertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint8:
					return new UhtByteProperty(propertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint16:
					return new UhtUInt16Property(propertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint32:
					return new UhtUInt32Property(propertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint64:
					return new UhtUInt64Property(propertySettings, this.UnderlyingTypeSize);
				default:
					throw new UhtIceException("Unexpected underlying enum type");
			}
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TEnumAsByte")]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtProperty? EnumProperty(UhtPropertyResolvePhase resolvePhase, UhtPropertySettings propertySettings, IUhtTokenReader tokenReader, UhtToken matchedToken)
		{
			UhtType outer = propertySettings.Outer;
			UhtEnum? enumObj = null;
			using (UhtMessageContext tokenContext = new UhtMessageContext("TEnumAsByte"))
			{
				tokenReader
					.Require("TEnumAsByte")
					.Require('<')
					.Optional("enum")
					.RequireCppIdentifier(UhtCppIdentifierOptions.None, (UhtTokenList tokenList) =>
					{
						enumObj = outer.FindType(UhtFindOptions.Enum | UhtFindOptions.SourceName, tokenList, tokenReader) as UhtEnum;
					})
					.Require('>');
			}
			return enumObj != null ? new UhtByteProperty(propertySettings, UhtPropertyIntType.None, enumObj) : null;
		}
		#endregion
	}
}
