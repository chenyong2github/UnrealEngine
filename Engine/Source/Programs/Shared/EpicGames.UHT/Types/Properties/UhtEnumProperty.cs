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
	[UhtEngineClass(Name = "EnumProperty", IsProperty = true)]
	public class UhtEnumProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "ByteProperty" : "EnumProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "invalid"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "PROPERTY" : "ENUM"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => this.Enum.CppForm != UhtEnumCppForm.EnumClass ? UhtPGetArgumentType.EngineClass : UhtPGetArgumentType.TypeText; }

		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtEnum>))]
		public UhtEnum Enum { get; set; }

		public UhtProperty? UnderlyingProperty { get; set; }

		public UhtEnumUnderlyingType UnderlyingType => this.Enum.UnderlyingType != UhtEnumUnderlyingType.Unspecified ? this.Enum.UnderlyingType : UhtEnumUnderlyingType.int32;
		public UhtPropertyIntType UnderlyingTypeSize => this.Enum.UnderlyingType != UhtEnumUnderlyingType.Unspecified ? UhtPropertyIntType.Sized : UhtPropertyIntType.Unsized;

		public UhtEnumProperty(UhtPropertySettings PropertySettings, UhtEnum Enum) : base(PropertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
			this.Enum = Enum;
			this.HeaderFile.AddReferencedHeader(Enum);
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
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			Collector.AddCrossModuleReference(this.Enum, true);
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

		public static StringBuilder AppendEnumText(StringBuilder Builder, UhtProperty Property, UhtEnum Enum, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.ClassFunction:
				case UhtPropertyTextType.InterfaceFunction:
				case UhtPropertyTextType.EventFunction:
					if (Enum.CppForm == UhtEnumCppForm.EnumClass || (!bIsTemplateArgument && (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm) || !Property.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))))
					{
						Builder.Append(Enum.CppType);
					}
					else
					{
						Builder.Append("TEnumAsByte<").Append(Enum.CppType).Append(">");
					}
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.Append(Enum.SourceName);
					break;

				default:
					if (Enum.CppForm == UhtEnumCppForm.EnumClass)
					{
						Builder.Append(Enum.CppType);
					}
					else
					{
						Builder.Append("TEnumAsByte<").Append(Enum.CppType).Append(">");
					}
					break;
			}
			return Builder;
		}

		/// <summary>
		/// Append the text for a function thunk call argument
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// 
		/// <returns>Output builder</returns>
		public static StringBuilder AppendEnumFunctionThunkParameterArg(StringBuilder Builder, UhtProperty Property, UhtEnum Enum)
		{
			if (!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
			{
				Builder.Append(Enum.CppType).Append('(').AppendFunctionThunkParameterName(Property).Append(')');
			}
			else if (Enum.CppForm == UhtEnumCppForm.EnumClass)
			{
				Builder.Append('(').Append(Enum.CppType).Append("&)(").AppendFunctionThunkParameterName(Property).Append(')');
			}
			else
			{
				Builder.Append("(TEnumAsByte<").Append(Enum.CppType).Append(">&)(").AppendFunctionThunkParameterName(Property).Append(')');
			}
			return Builder;
		}

		public static string? GetEnumRigVMType(UhtProperty Property, UhtEnum Enum, ref UhtRigVMParameterFlags ParameterFlags)
		{
			ParameterFlags |= UhtRigVMParameterFlags.IsEnum;
			return Enum.CppType.ToString();
		}

		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			return AppendEnumText(Builder, this, this.Enum, TextType, bIsTemplateArgument);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			if (this.UnderlyingProperty != null)
			{
				Builder.AppendMemberDecl(this.UnderlyingProperty, Context, Name, GetNameSuffix(NameSuffix, "_Underlying"), Tabs);
			}
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, this.Enum.CppForm != UhtEnumCppForm.EnumClass ? "FBytePropertyParams" : "FEnumPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			if (this.Enum.CppForm == UhtEnumCppForm.EnumClass)
			{
				if (this.UnderlyingProperty != null)
				{
					Builder.AppendMemberDef(this.UnderlyingProperty, Context, Name, GetNameSuffix(NameSuffix, "_Underlying"), "0", Tabs);
				}
				AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FEnumPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Enum");
				AppendMemberDefRef(Builder, Context, this.Enum, true);
				AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
				return Builder;
			}
			else
			{
				AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FBytePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Byte", true, true);
				AppendMemberDefRef(Builder, Context, this.Enum, true);
				AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberPtr(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			if (this.UnderlyingProperty != null)
			{
				Builder.AppendMemberPtr(this.UnderlyingProperty, Context, Name, GetNameSuffix(NameSuffix, "_Underlying"), Tabs);
			}
			base.AppendMemberPtr(Builder, Context, Name, NameSuffix, Tabs);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterArg(StringBuilder Builder)
		{
			return AppendEnumFunctionThunkParameterArg(Builder, this, this.Enum);
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
			Builder.AppendObjectHash(StartingLength, this, Context, this.Enum);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			if (this.Enum.CppForm != UhtEnumCppForm.EnumClass)
			{
				Builder.Append("0");
			}
			else
			{
				Builder.Append('(').Append(this.Enum.CppType).Append(")0");
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			return SanitizeEnumDefaultValue(this, this.Enum, DefaultValueReader, InnerDefaultValue);
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtEnumProperty OtherEnum)
			{
				return this.Enum == OtherEnum.Enum;
			}
			else if (Other is UhtByteProperty OtherByte)
			{
				return this.Enum == OtherByte.Enum;
			}
			return false;
		}

		public static bool SanitizeEnumDefaultValue(UhtProperty Property, UhtEnum Enum, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			UhtTokenList CppIdentifier = DefaultValueReader.GetCppIdentifier(UhtCppIdentifierOptions.None);
			UhtTokenList StartingPoint = CppIdentifier.Next != null ? CppIdentifier.Next : CppIdentifier;
			StartingPoint.Join(InnerDefaultValue, "::");
			UhtTokenListCache.Return(CppIdentifier);

			int EntryIndex = Enum.GetIndexByName(InnerDefaultValue.ToString());
			if (EntryIndex == -1)
			{
				return false;
			}
			if (Enum.MetaData.ContainsKey(UhtNames.Hidden, EntryIndex))
			{
				Property.LogError($"Hidden enum entries cannot be used as default values: '{Property.SourceName}' '{InnerDefaultValue}'");
			}
			return true;
		}

		private UhtProperty CreateUnderlyingProperty()
		{
			UhtPropertySettings PropertySettings = new UhtPropertySettings();
			PropertySettings.Reset(this, 0, this.PropertyCategory, 0);
			PropertySettings.SourceName = "UnderlyingType";
			switch (this.UnderlyingType)
			{
				case UhtEnumUnderlyingType.int8:
					return new UhtInt8Property(PropertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.int16:
					return new UhtInt16Property(PropertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.int32:
					return new UhtIntProperty(PropertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.int64:
					return new UhtInt64Property(PropertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint8:
					return new UhtByteProperty(PropertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint16:
					return new UhtUInt16Property(PropertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint32:
					return new UhtUInt32Property(PropertySettings, this.UnderlyingTypeSize);
				case UhtEnumUnderlyingType.uint64:
					return new UhtUInt64Property(PropertySettings, this.UnderlyingTypeSize);
				default:
					throw new UhtIceException("Unexpected underlying enum type");
			}
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return GetEnumRigVMType(this, this.Enum, ref ParameterFlags);
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TEnumAsByte")]
		public static UhtProperty? EnumProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtType Outer = PropertySettings.Outer;
			UhtEnum? Enum = null;
			using (var TokenContext = new UhtMessageContext(TokenReader, "TEnumAsByte"))
			{
				TokenReader
					.Require("TEnumAsByte")
					.Require('<')
					.Optional("enum")
					.RequireCppIdentifier(UhtCppIdentifierOptions.None, (UhtTokenList TokenList) =>
					{
						Enum = Outer.FindType(UhtFindOptions.Enum | UhtFindOptions.SourceName, TokenList, TokenReader) as UhtEnum;
					})
					.Require('>');
			}
			return Enum != null ? new UhtByteProperty(PropertySettings, UhtPropertyIntType.None, Enum) : null;
		}
		#endregion
	}
}
