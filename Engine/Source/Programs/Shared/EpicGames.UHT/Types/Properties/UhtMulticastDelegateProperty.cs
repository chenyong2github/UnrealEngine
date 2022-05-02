// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text;
using System.Text.Json.Serialization;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// FMulticastDelegateProperty
	/// </summary>
	[UhtEngineClass(Name = "MulticastDelegateProperty", IsProperty = true)]
	public abstract class UhtMulticastDelegateProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName => "MulticastDelegateProperty";

		/// <inheritdoc/>
		protected override string CppTypeText => "FMulticastScriptDelegate";

		/// <inheritdoc/>
		protected override string PGetMacroText => "PROPERTY";

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument => UhtPGetArgumentType.TypeText;

		/// <summary>
		/// Referenced function
		/// </summary>
		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtFunction>))]
		public UhtFunction Function { get; set; }

		/// <summary>
		/// Construct property
		/// </summary>
		/// <param name="propertySettings">Property settings</param>
		/// <param name="function">Referenced function</param>
		protected UhtMulticastDelegateProperty(UhtPropertySettings propertySettings, UhtFunction function) : base(propertySettings)
		{
			this.Function = function;
			this.HeaderFile.AddReferencedHeader(function);
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.CanBeBlueprintAssignable | UhtPropertyCaps.CanBeBlueprintCallable |
				UhtPropertyCaps.CanBeBlueprintAuthorityOnly | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.SupportsRigVM;
			this.PropertyCaps &= ~(UhtPropertyCaps.IsParameterSupportedByBlueprint);
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase phase)
		{
			bool results = base.ResolveSelf(phase);
			switch (phase)
			{
				case UhtResolvePhase.Final:
					this.PropertyFlags |= EPropertyFlags.InstancedReference & ~this.DisallowPropertyFlags;
					break;
			}
			return results;
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool deepScan)
		{
			return true;
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector collector, bool templateProperty)
		{
			collector.AddCrossModuleReference(this.Function, true);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder builder, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			switch (textType)
			{
				case UhtPropertyTextType.EventParameterFunctionMember:
				case UhtPropertyTextType.ExportMember:
				case UhtPropertyTextType.RigVMType:
				case UhtPropertyTextType.FunctionThunkParameterArgType:
					builder.Append(this.CppTypeText);
					break;

				default:
					builder.Append(this.Function.SourceName);
					break;
			}
			return builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder builder, int startingLength, IUhtPropertyMemberContext context)
		{
			builder.AppendObjectHash(startingLength, this, context, this.Function);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterArg(StringBuilder builder)
		{
			return builder.Append(this.Function.SourceName).Append('(').AppendFunctionThunkParameterName(this).Append(')');
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder builder, bool isInitializer)
		{
			builder.AppendPropertyText(this, UhtPropertyTextType.Construction).Append("()");
			return builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader defaultValueReader, StringBuilder innerDefaultValue)
		{
			return false;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty other)
		{
			if (other is UhtMulticastDelegateProperty otherObject)
			{
				return this.Function == otherObject.Function;
			}
			return false;
		}
	}
}
