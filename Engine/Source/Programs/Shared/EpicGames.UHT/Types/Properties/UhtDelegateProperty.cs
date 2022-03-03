// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	[UhtEngineClass(Name = "DelegateProperty", IsProperty = true)]
	public class UhtDelegateProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "DelegateProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "FScriptDelegate"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "PROPERTY"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.EngineClass; }

		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtFunction>))]
		public UhtFunction Function { get; set; }

		public UhtDelegateProperty(UhtPropertySettings PropertySettings, UhtFunction Function) : base(PropertySettings)
		{
			this.Function = Function;
			this.HeaderFile.AddReferencedHeader(Function);
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResults = base.ResolveSelf(Phase);
			switch (Phase)
			{
				case UhtResolvePhase.Final:
					this.PropertyFlags |= EPropertyFlags.InstancedReference & ~this.DisallowPropertyFlags;
					break;
			}
			return bResults;
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool bDeepScan)
		{
			return true;
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			Collector.AddCrossModuleReference(this.Function, true);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.ExportMember:
				case UhtPropertyTextType.RigVMTemplateArg:
				case UhtPropertyTextType.EventParameterFunctionMember:
					Builder.Append(this.CppTypeText);
					break;

				default:
					Builder.Append(this.Function.SourceName);
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FDelegatePropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FDelegatePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Delegate");
			AppendMemberDefRef(Builder, Context, this.Function, true);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterArg(StringBuilder Builder)
		{
			return Builder.Append(this.Function.SourceName).Append("(").AppendFunctionThunkParameterName(this).Append(")");
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
			Builder.AppendObjectHash(StartingLength, this, Context, this.Function);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.AppendPropertyText(this, UhtPropertyTextType.Construction).Append("()");
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
			if (Other is UhtDelegateProperty OtherObject)
			{
				return this.Function == OtherObject.Function;
			}
			return false;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction Function, UhtValidationOptions Options)
		{
			base.ValidateFunctionArgument(Function, Options);

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest))
				{
					if (!this.PropertyFlags.HasAnyFlags(EPropertyFlags.RepSkip))
					{
						this.LogError("Service request functions cannot contain delegate parameters, unless marked NotReplicated");
					}
				}
				else
				{
					this.LogError("Replicated functions cannot contain delegate parameters (this would be insecure)");
				}
			}
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return this.CppTypeText;
		}
	}
}
