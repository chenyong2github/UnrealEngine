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
	[UhtEngineClass(Name = "MapProperty", IsProperty = true)]
	public class UhtMapProperty : UhtContainerBaseProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "MapProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "TMap"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "TMAP"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		public UhtProperty KeyProperty { get; set; }

		public UhtMapProperty(UhtPropertySettings PropertySettings, UhtProperty Key, UhtProperty Value) : base(PropertySettings, Value)
		{
			// If the creation of the value property set more flags, then copy those flags to ourselves
			this.PropertyFlags |= Value.PropertyFlags & EPropertyFlags.UObjectWrapper;

			// Make sure the 'UObjectWrapper' flag is maintained so that both 'TMap<TSubclassOf<...>, ...>' and 'TMap<UClass*, TSubclassOf<...>>' works correctly
			Key.PropertyFlags = (Value.PropertyFlags & ~EPropertyFlags.UObjectWrapper) | (Key.PropertyFlags & EPropertyFlags.UObjectWrapper);
			Key.DisallowPropertyFlags = ~(EPropertyFlags.ContainsInstancedReference | EPropertyFlags.InstancedReference | EPropertyFlags.UObjectWrapper);

			this.KeyProperty = Key;
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef;
			this.PropertyCaps &= ~(UhtPropertyCaps.CanBeContainerValue | UhtPropertyCaps.CanBeContainerKey);
			this.PropertyCaps = (this.PropertyCaps & ~(UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn));
			this.PropertyCaps |= (this.ValueProperty.PropertyCaps & (UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn)) &
				(this.KeyProperty.PropertyCaps & (UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint));
			this.PropertyFlags = Value.PropertyFlags;
			this.ValueProperty.SourceName = this.SourceName;
			this.ValueProperty.EngineName = this.EngineName;
			this.ValueProperty.PropertyFlags &= EPropertyFlags.PropagateToMapValue;
			this.ValueProperty.Outer = this;
			this.ValueProperty.MetaData.Clear();
			this.KeyProperty.SourceName = $"{this.SourceName}_Key";
			this.KeyProperty.EngineName = $"{this.EngineName}_Key";
			this.KeyProperty.PropertyFlags &= EPropertyFlags.PropagateToMapKey;
			this.KeyProperty.Outer = this;
			this.KeyProperty.MetaData.Clear();

			// With old UHT, Deprecated was applied after property create.
			// With the new, it is applied prior to creation.  Deprecated in old 
			// was not on the key.
			this.KeyProperty.PropertyFlags &= ~EPropertyFlags.Deprecated;
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResults = base.ResolveSelf(Phase);
			switch (Phase)
			{
				case UhtResolvePhase.Final:
					this.KeyProperty.Resolve(Phase);
					this.KeyProperty.MetaData.Clear();

					EPropertyFlags NewFlags = ResolveAndReturnNewFlags(this.ValueProperty, Phase);
					this.PropertyFlags |= NewFlags;
					this.KeyProperty.PropertyFlags |= NewFlags;
					this.MetaData.Add(this.ValueProperty.MetaData);
					this.ValueProperty.MetaData.Clear();

					PropagateFlagsFromInnerAndHandlePersistentInstanceMetadata(this, null, this.KeyProperty);
					PropagateFlagsFromInnerAndHandlePersistentInstanceMetadata(this, this.MetaData, this.ValueProperty);
					break;
			}
			return bResults;
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			this.ValueProperty.CollectReferencesInternal(Collector, true);
			this.KeyProperty.CollectReferencesInternal(Collector, true);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return $"{this.KeyProperty.GetForwardDeclarations()} {this.ValueProperty.GetForwardDeclarations()}";
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			foreach (UhtType Type in this.ValueProperty.EnumerateReferencedTypes())
			{
				yield return Type;
			}
			foreach (UhtType Type in this.KeyProperty.EnumerateReferencedTypes())
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
					Builder.Append("TMap");
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.AppendFunctionThunkParameterArrayType(this.KeyProperty).Append(",").AppendFunctionThunkParameterArrayType(this.ValueProperty);
					break;

				default:
					Builder.Append("TMap<").AppendPropertyText(this.KeyProperty, TextType, true).Append(',').AppendPropertyText(this.ValueProperty, TextType, true).Append('>');
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			Builder.AppendMemberDecl(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_ValueProp"), Tabs);
			Builder.AppendMemberDecl(this.KeyProperty, Context, Name, GetNameSuffix(NameSuffix, "_Key_KeyProp"), Tabs);
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FMapPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			Builder.AppendMemberDef(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_ValueProp"), "1", Tabs);
			Builder.AppendMemberDef(this.KeyProperty, Context, Name, GetNameSuffix(NameSuffix, "_Key_KeyProp"), "0", Tabs);
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FMapPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Map");
			Builder.Append(this.Allocator == UhtPropertyAllocator.MemoryImage ? "EMapPropertyFlags::UsesMemoryImageAllocator" : "EMapPropertyFlags::None").Append(", ");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberPtr(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			Builder.AppendMemberPtr(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_ValueProp"), Tabs);
			Builder.AppendMemberPtr(this.KeyProperty, Context, Name, GetNameSuffix(NameSuffix, "_Key_KeyProp"), Tabs);
			base.AppendMemberPtr(Builder, Context, Name, NameSuffix, Tabs);
			return Builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
			this.KeyProperty.AppendObjectHashes(Builder, StartingLength, Context);
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
			if (Other is UhtMapProperty OtherMap)
			{
				return this.ValueProperty.IsSameType(OtherMap.ValueProperty) &&
					this.KeyProperty.IsSameType(OtherMap.KeyProperty);
			}
			return false;
		}

		/// <inheritdoc/>
		public override void ValidateDeprecated()
		{
			base.ValidateDeprecated();
			this.KeyProperty.ValidateDeprecated();
		}

		/// <inheritdoc/>
		public override void Validate(UhtStruct OuterStruct, UhtProperty OutermostProperty, UhtValidationOptions Options)
		{
			base.Validate(OuterStruct, OutermostProperty, Options);
			this.KeyProperty.Validate(OuterStruct, OutermostProperty, Options | UhtValidationOptions.IsKey);
		}

		///<inheritdoc/>
		public override bool ValidateStructPropertyOkForNet(UhtProperty ReferencingProperty)
		{
			if (!this.PropertyFlags.HasAnyFlags(EPropertyFlags.RepSkip))
			{
				ReferencingProperty.LogError($"Maps are not supported for Replication or RPCs.  Map '{this.SourceName}' in '{this.Outer?.SourceName}'.  Origin '{ReferencingProperty.SourceName}'");
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
					this.LogError("Maps are not supported in an RPC.");
				}
			}
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return null;
		}

#region Keyword
		[UhtPropertyType(Keyword = "TMap")]
		public static UhtProperty? MapProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			using (var TokenContext = new UhtMessageContext(TokenReader, "TMap"))
			{
				if (!TokenReader.SkipExpectedType(MatchedToken.Value, PropertySettings.PropertyCategory == UhtPropertyCategory.Member))
				{
					return null;
				}
				TokenReader.Require('<');

				// Parse the key type
				UhtProperty? Key = UhtPropertyParser.ParseTemplateParam(ResolvePhase, PropertySettings, "Key", TokenReader);
				if (Key == null)
				{
					return null;
				}

				if (!Key.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeContainerKey))
				{
					TokenReader.LogError($"The type \'{Key.GetUserFacingDecl()}\' can not be used as a key in a TMap");
				}

				if (PropertySettings.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
				{
					TokenReader.LogError("Replicated maps are not supported.");
				}

				TokenReader.Require(',');

				// Parse the value type
				UhtProperty? Value = UhtPropertyParser.ParseTemplateParam(ResolvePhase, PropertySettings, "Value", TokenReader);
				if (Value == null)
				{
					return null;
				}

				if (!Value.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeContainerValue))
				{
					TokenReader.LogError($"The type \'{Value.GetUserFacingDecl()}\' can not be used as a value in a TMap");
				}

				if (TokenReader.TryOptional(','))
				{
					UhtToken AllocatorToken = TokenReader.GetIdentifier();
					if (AllocatorToken.IsIdentifier("FMemoryImageSetAllocator"))
					{
						PropertySettings.Allocator = UhtPropertyAllocator.MemoryImage;
					}
					else
					{
						TokenReader.LogError($"Found '{AllocatorToken.Value}' - explicit allocators are not supported in TMap properties.");
					}
				}
				TokenReader.Require('>');

				//@TODO: Prevent sparse delegate types from being used in a container

				return new UhtMapProperty(PropertySettings, Key, Value);
			}
		}
#endregion
	}
}
