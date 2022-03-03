// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "FieldPathProperty", IsProperty = true)]
	public class UhtFieldPathProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "FieldPathProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "TFieldPath"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "STRUCT"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		/// <summary>
		/// Class name without the prefix
		/// </summary>
		public string FieldClassName { get; set; }

		public UhtFieldPathProperty(UhtPropertySettings PropertySettings, string FieldClassName) : base(PropertySettings)
		{
			this.FieldClassName = FieldClassName;
			this.PropertyCaps |= UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return $"class F{this.FieldClassName};";
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.SparseShort:
					Builder.Append("TFieldPath");
					break;

				default:
					Builder.Append("TFieldPath<F").Append(this.FieldClassName).Append('>');
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FFieldPathPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FFieldPathPropertyParams", "UECodeGen_Private::EPropertyGenFlags::FieldPath");
			Builder.Append("&F").Append(this.FieldClassName).Append("::StaticClass, ");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("nullptr");
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			return false;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtFieldPathProperty OtherFieldPath)
			{
				return this.FieldClassName == OtherFieldPath.FieldClassName;
			}
			return false;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return null;
		}

#region Keyword
		[UhtPropertyType(Keyword = "TFieldPath", Options = UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? FieldPathProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			using (var TokenContext = new UhtMessageContext(TokenReader, "TFieldPath"))
			{
				UhtToken Identifier = new UhtToken();
				TokenReader
					.Require("TFieldPath")
					.Require('<')
					.RequireIdentifier((ref UhtToken Token) => Identifier = Token)
					.Require('>');

				StringView FieldClassName = new StringView(Identifier.Value, 1);
				if (!UhtEngineClassTable.Instance.IsValidPropertyTypeName(FieldClassName))
				{
					throw new UhtException($"Undefined property type: {Identifier.Value}");
				}
				return new UhtFieldPathProperty(PropertySettings, FieldClassName.ToString());
			}
		}
#endregion
	}
}
