// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "StrProperty", IsProperty = true)]
	public class UhtStrProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "StrProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "FString"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "PROPERTY"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.EngineClass; }

		public UhtStrProperty(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("TEXT(\"\")");
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FStrPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FStrPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Str");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			if (DefaultValueReader.TryOptional("FString"))
			{
				DefaultValueReader.Require('(');
				StringView Value = DefaultValueReader.GetWrappedConstString();
				DefaultValueReader.Require(')');
				InnerDefaultValue.Append(Value);
			}
			else
			{
				StringView Value = DefaultValueReader.GetWrappedConstString();
				InnerDefaultValue.Append(Value);
			}
			return true;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtStrProperty;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction Function, UhtValidationOptions Options)
		{
			base.ValidateFunctionArgument(Function, Options);

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (!Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest))
				{
					if (this.RefQualifier != UhtPropertyRefQualifier.ConstRef && !this.bIsStaticArray)
					{
						this.LogError("Replicated FString parameters must be passed by const reference");
					}
				}
			}
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return this.CppTypeText;
		}

		[UhtPropertyType(Keyword = "FString")]
		[UhtPropertyType(Keyword = "FMemoryImageString", Options = UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? StrProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			if (!TokenReader.SkipExpectedType(MatchedToken.Value, PropertySettings.PropertyCategory == UhtPropertyCategory.Member))
			{
				return null;
			}
			UhtStrProperty Out = new UhtStrProperty(PropertySettings);
			if (Out.PropertyCategory != UhtPropertyCategory.Member)
			{
				if (TokenReader.TryOptional('&'))
				{
					if (Out.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
					{
						// 'const FString& Foo' came from 'FString' in .uc, no flags
						Out.PropertyFlags &= ~EPropertyFlags.ConstParm;

						// We record here that we encountered a const reference, because we need to remove that information from flags for code generation purposes.
						Out.RefQualifier = UhtPropertyRefQualifier.ConstRef;
					}
					else
					{
						// 'FString& Foo' came from 'out FString' in .uc
						Out.PropertyFlags |= EPropertyFlags.OutParm;

						// And we record here that we encountered a non-const reference here too.
						Out.RefQualifier = UhtPropertyRefQualifier.NonConstRef;
					}
				}
			}
			return Out;
		}
	}
}
