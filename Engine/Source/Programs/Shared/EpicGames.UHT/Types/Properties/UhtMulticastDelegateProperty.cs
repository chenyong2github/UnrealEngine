// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	[UhtEngineClass(Name = "MulticastDelegateProperty", IsProperty = true)]
	public abstract class UhtMulticastDelegateProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "MulticastDelegateProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "FMulticastScriptDelegate"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "PROPERTY"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtFunction>))]
		public UhtFunction Function { get; set; }

		public UhtMulticastDelegateProperty(UhtPropertySettings PropertySettings, UhtFunction Function) : base(PropertySettings)
		{
			this.Function = Function;
			this.HeaderFile.AddReferencedHeader(Function);
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.CanBeBlueprintAssignable | UhtPropertyCaps.CanBeBlueprintCallable |
				UhtPropertyCaps.CanBeBlueprintAuthorityOnly | UhtPropertyCaps.IsMemberSupportedByBlueprint;
			this.PropertyCaps &= ~(UhtPropertyCaps.IsParameterSupportedByBlueprint);
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
				case UhtPropertyTextType.EventParameterFunctionMember:
				case UhtPropertyTextType.ExportMember:
				case UhtPropertyTextType.RigVMTemplateArg:
				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.Append("FMulticastScriptDelegate");
					break;

				default:
					Builder.Append(this.Function.SourceName);
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
			Builder.AppendObjectHash(StartingLength, this, Context, this.Function);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterArg(StringBuilder Builder)
		{
			return Builder.Append(this.Function.SourceName).Append("(").AppendFunctionThunkParameterName(this).Append(")");
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
			if (Other is UhtMulticastDelegateProperty OtherObject)
			{
				return this.Function == OtherObject.Function;
			}
			return false;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return this.CppTypeText;
		}
	}
}
