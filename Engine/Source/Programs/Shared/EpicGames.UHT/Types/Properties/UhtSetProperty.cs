// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "SetProperty", IsProperty = true)]
	public class UhtSetProperty : UhtContainerBaseProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "SetProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "TSet"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "TSET"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		public UhtSetProperty(UhtPropertySettings PropertySettings, UhtProperty Value) : base(PropertySettings, Value)
		{
			// If the creation of the value property set more flags, then copy those flags to ourselves
			this.PropertyFlags |= Value.PropertyFlags & EPropertyFlags.UObjectWrapper;

			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef;
			this.PropertyCaps &= ~(UhtPropertyCaps.CanBeContainerValue | UhtPropertyCaps.CanBeContainerKey);
			this.PropertyCaps = (this.PropertyCaps & ~(UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn)) |
				(this.ValueProperty.PropertyCaps & (UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn));
			this.ValueProperty.SourceName = this.SourceName;
			this.ValueProperty.EngineName = this.EngineName;
			this.ValueProperty.PropertyFlags = this.PropertyFlags & EPropertyFlags.PropagateToSetElement;
			this.ValueProperty.Outer = this;
			this.ValueProperty.MetaData.Clear();
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResults = base.ResolveSelf(Phase);
			switch (Phase)
			{
				case UhtResolvePhase.Final:
					this.PropertyFlags |= ResolveAndReturnNewFlags(this.ValueProperty, Phase);
					this.MetaData.Add(this.ValueProperty.MetaData);
					this.ValueProperty.MetaData.Clear();
					PropagateFlagsFromInnerAndHandlePersistentInstanceMetadata(this, this.MetaData, this.ValueProperty);
					break;
			}
			return bResults;
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			this.ValueProperty.CollectReferencesInternal(Collector, true);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return this.ValueProperty.GetForwardDeclarations();
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			foreach (UhtType Type in this.ValueProperty.EnumerateReferencedTypes())
			{
				yield return Type;
			}
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.SparseShort:
					Builder.Append("TSet");
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.AppendFunctionThunkParameterArrayType(this.ValueProperty);
					break;

				default:
					Builder.Append("TSet<").AppendPropertyText(this.ValueProperty, TextType, true);
					if (Builder[Builder.Length - 1] == '>')
					{
						// if our internal property type is a template class, add a space between the closing brackets b/c VS.NET cannot parse this correctly
						Builder.Append(" ");
					}
					Builder.Append('>');
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			Builder.AppendMemberDecl(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_ElementProp"), Tabs);
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FSetPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			Builder.AppendMemberDef(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_ElementProp"), "0", Tabs);
			Builder.AppendMetaDataDef(this, Context, Name, NameSuffix, Tabs);

			if (this.ValueProperty is UhtStructProperty StructProperty)
			{
				Builder
					.AppendTabs(Tabs)
					.Append("static_assert(TModels<CGetTypeHashable, ")
					.Append(StructProperty.ScriptStruct.SourceName)
					.Append(">::Value, \"The structure '")
					.Append(StructProperty.ScriptStruct.SourceName)
					.Append("' is used in a TSet but does not have a GetValueTypeHash defined\");\r\n");
			}

			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FSetPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Set", false);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberPtr(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			Builder.AppendMemberPtr(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_ElementProp"), Tabs);
			base.AppendMemberPtr(Builder, Context, Name, NameSuffix, Tabs);
			return Builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
			this.ValueProperty.AppendObjectHashes(Builder, StartingLength, Context);
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
			if (Other is UhtSetProperty OtherSet)
			{
				return this.ValueProperty.IsSameType(OtherSet.ValueProperty);
			}
			return false;
		}

		///<inheritdoc/>
		public override bool ValidateStructPropertyOkForNet(UhtProperty ReferencingProperty)
		{
			if (!this.PropertyFlags.HasAnyFlags(EPropertyFlags.RepSkip))
			{
				ReferencingProperty.LogError($"Sets are not supported for Replication or RPCs.  Set '{this.SourceName}' in '{this.Outer?.SourceName}'.  Origin '{ReferencingProperty.SourceName}'");
				return false;
			}
			return true;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction Function, UhtValidationOptions Options)
		{
			base.ValidateFunctionArgument(Function, Options);

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (!Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest | EFunctionFlags.NetResponse))
				{
					this.LogError("Sets are not supported in an RPC.");
				}
			}
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return null;
		}

#region Keyword
		[UhtPropertyType(Keyword = "TSet")]
		public static UhtProperty? SetProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			using (var TokenContext = new UhtMessageContext(TokenReader, "TSet"))
			{
				if (!TokenReader.SkipExpectedType(MatchedToken.Value, PropertySettings.PropertyCategory == UhtPropertyCategory.Member))
				{
					return null;
				}
				TokenReader.Require('<');

				// Parse the value type
				UhtProperty? Value = UhtPropertyParser.ParseTemplateParam(ResolvePhase, PropertySettings, "Value", TokenReader);
				if (Value == null)
				{
					return null;
				}

				if (!Value.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeContainerKey))
				{
					TokenReader.LogError($"The type \'{Value.GetUserFacingDecl()}\' can not be used as a key in a TSet");
				}

				if (PropertySettings.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
				{
					TokenReader.LogError("Replicated sets are not supported.");
				}

				if (TokenReader.TryOptional(','))
				{
					// If we found a comma, read the next thing, assume it's a keyfuncs, and report that
					UhtToken KeyFuncToken = TokenReader.GetIdentifier();
					throw new UhtException(TokenReader, $"Found '{KeyFuncToken.Value}' - explicit KeyFuncs are not supported in TSet properties.");
				}

				TokenReader.Require('>');

				//@TODO: Prevent sparse delegate types from being used in a container

				return new UhtSetProperty(PropertySettings, Value);
			}
		}
#endregion
	}
}
