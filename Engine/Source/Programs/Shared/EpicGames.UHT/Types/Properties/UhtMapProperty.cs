// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// FMapProperty
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "MapProperty", IsProperty = true)]
	public class UhtMapProperty : UhtContainerBaseProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName => "MapProperty";

		/// <inheritdoc/>
		protected override string CppTypeText => "TMap";

		/// <inheritdoc/>
		protected override string PGetMacroText => "TMAP";

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument => UhtPGetArgumentType.TypeText;

		/// <summary>
		/// Key property
		/// </summary>
		public UhtProperty KeyProperty { get; set; }

		/// <summary>
		/// Construct new property
		/// </summary>
		/// <param name="propertySettings">Property settings</param>
		/// <param name="key">Key property</param>
		/// <param name="value">Value property</param>
		public UhtMapProperty(UhtPropertySettings propertySettings, UhtProperty key, UhtProperty value) : base(propertySettings, value)
		{
			// If the creation of the value property set more flags, then copy those flags to ourselves
			this.PropertyFlags |= value.PropertyFlags & EPropertyFlags.UObjectWrapper;

			// Make sure the 'UObjectWrapper' flag is maintained so that both 'TMap<TSubclassOf<...>, ...>' and 'TMap<UClass*, TSubclassOf<...>>' works correctly
			key.PropertyFlags = (value.PropertyFlags & ~EPropertyFlags.UObjectWrapper) | (key.PropertyFlags & EPropertyFlags.UObjectWrapper);
			key.DisallowPropertyFlags = ~(EPropertyFlags.ContainsInstancedReference | EPropertyFlags.InstancedReference | EPropertyFlags.UObjectWrapper);

			this.KeyProperty = key;
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef;
			this.PropertyCaps &= ~(UhtPropertyCaps.CanBeContainerValue | UhtPropertyCaps.CanBeContainerKey);
			this.PropertyCaps &= ~(UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn);
			this.PropertyCaps |= (this.ValueProperty.PropertyCaps & (UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn)) &
				(this.KeyProperty.PropertyCaps & (UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint));
			this.PropertyFlags = value.PropertyFlags;
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
		protected override bool ResolveSelf(UhtResolvePhase phase)
		{
			bool results = base.ResolveSelf(phase);
			switch (phase)
			{
				case UhtResolvePhase.Final:
					this.KeyProperty.Resolve(phase);
					this.KeyProperty.MetaData.Clear();

					EPropertyFlags newFlags = ResolveAndReturnNewFlags(this.ValueProperty, phase);
					this.PropertyFlags |= newFlags;
					this.KeyProperty.PropertyFlags |= newFlags;
					this.MetaData.Add(this.ValueProperty.MetaData);
					this.ValueProperty.MetaData.Clear();
					this.ValueProperty.PropertyFlags = this.PropertyFlags & EPropertyFlags.PropagateToMapValue;

					PropagateFlagsFromInnerAndHandlePersistentInstanceMetadata(this, null, this.KeyProperty);
					PropagateFlagsFromInnerAndHandlePersistentInstanceMetadata(this, this.MetaData, this.ValueProperty);
					break;
			}
			return results;
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector collector, bool templateProperty)
		{
			this.ValueProperty.CollectReferencesInternal(collector, true);
			this.KeyProperty.CollectReferencesInternal(collector, true);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return $"{this.KeyProperty.GetForwardDeclarations()} {this.ValueProperty.GetForwardDeclarations()}";
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			foreach (UhtType type in this.ValueProperty.EnumerateReferencedTypes())
			{
				yield return type;
			}
			foreach (UhtType type in this.KeyProperty.EnumerateReferencedTypes())
			{
				yield return type;
			}
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder builder, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			switch (textType)
			{
				case UhtPropertyTextType.SparseShort:
					builder.Append("TMap");
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					builder.AppendFunctionThunkParameterArrayType(this.KeyProperty).Append(',').AppendFunctionThunkParameterArrayType(this.ValueProperty);
					break;

				default:
					builder.Append("TMap<").AppendPropertyText(this.KeyProperty, textType, true);
					if (builder[^1] == '>')
					{
						// if our internal property type is a template class, add a space between the closing brackets b/c VS.NET cannot parse this correctly
						builder.Append(' ');
					}
					builder.Append(',').AppendPropertyText(this.ValueProperty, textType, true);
					if (builder[^1] == '>')
					{
						// if our internal property type is a template class, add a space between the closing brackets b/c VS.NET cannot parse this correctly
						builder.Append(' ');
					}
					builder.Append('>');
					break;
			}
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			builder.AppendMemberDecl(this.ValueProperty, context, name, GetNameSuffix(nameSuffix, "_ValueProp"), tabs);
			builder.AppendMemberDecl(this.KeyProperty, context, name, GetNameSuffix(nameSuffix, "_Key_KeyProp"), tabs);
			return AppendMemberDecl(builder, context, name, nameSuffix, tabs, "FMapPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, string? offset, int tabs)
		{
			builder.AppendMemberDef(this.ValueProperty, context, name, GetNameSuffix(nameSuffix, "_ValueProp"), "1", tabs);
			builder.AppendMemberDef(this.KeyProperty, context, name, GetNameSuffix(nameSuffix, "_Key_KeyProp"), "0", tabs);
			AppendMemberDefStart(builder, context, name, nameSuffix, offset, tabs, "FMapPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Map");
			builder.Append(this.Allocator == UhtPropertyAllocator.MemoryImage ? "EMapPropertyFlags::UsesMemoryImageAllocator" : "EMapPropertyFlags::None").Append(", ");
			AppendMemberDefEnd(builder, context, name, nameSuffix);
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberPtr(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			builder.AppendMemberPtr(this.ValueProperty, context, name, GetNameSuffix(nameSuffix, "_ValueProp"), tabs);
			builder.AppendMemberPtr(this.KeyProperty, context, name, GetNameSuffix(nameSuffix, "_Key_KeyProp"), tabs);
			base.AppendMemberPtr(builder, context, name, nameSuffix, tabs);
			return builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder builder, int startingLength, IUhtPropertyMemberContext context)
		{
			this.KeyProperty.AppendObjectHashes(builder, startingLength, context);
			this.ValueProperty.AppendObjectHashes(builder, startingLength, context);
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
			if (other is UhtMapProperty otherMap)
			{
				return this.ValueProperty.IsSameType(otherMap.ValueProperty) &&
					this.KeyProperty.IsSameType(otherMap.KeyProperty);
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
		public override void Validate(UhtStruct outerStruct, UhtProperty outermostProperty, UhtValidationOptions options)
		{
			base.Validate(outerStruct, outermostProperty, options);
			this.KeyProperty.Validate(outerStruct, outermostProperty, options | UhtValidationOptions.IsKey);
		}

		///<inheritdoc/>
		public override bool ValidateStructPropertyOkForNet(UhtProperty referencingProperty)
		{
			if (!this.PropertyFlags.HasAnyFlags(EPropertyFlags.RepSkip))
			{
				referencingProperty.LogError($"Maps are not supported for Replication or RPCs.  Map '{this.SourceName}' in '{this.Outer?.SourceName}'.  Origin '{referencingProperty.SourceName}'");
				return false;
			}
			return true;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction function, UhtValidationOptions options)
		{
			base.ValidateFunctionArgument(function, options);

			if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (!function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest | EFunctionFlags.NetResponse))
				{
					this.LogError("Maps are not supported in an RPC.");
				}
			}
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TMap")]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		private static UhtProperty? MapProperty(UhtPropertyResolvePhase resolvePhase, UhtPropertySettings propertySettings, IUhtTokenReader tokenReader, UhtToken matchedToken)
		{
			using (UhtMessageContext tokenContext = new UhtMessageContext("TMap"))
			{
				if (!tokenReader.SkipExpectedType(matchedToken.Value, propertySettings.PropertyCategory == UhtPropertyCategory.Member))
				{
					return null;
				}
				tokenReader.Require('<');

				// Parse the key type
				UhtProperty? key = UhtPropertyParser.ParseTemplateParam(resolvePhase, propertySettings, "Key", tokenReader);
				if (key == null)
				{
					return null;
				}

				if (!key.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeContainerKey))
				{
					tokenReader.LogError($"The type \'{key.GetUserFacingDecl()}\' can not be used as a key in a TMap");
				}

				if (propertySettings.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
				{
					tokenReader.LogError("Replicated maps are not supported.");
				}

				tokenReader.Require(',');

				// Parse the value type
				UhtProperty? value = UhtPropertyParser.ParseTemplateParam(resolvePhase, propertySettings, "Value", tokenReader);
				if (value == null)
				{
					return null;
				}

				if (!value.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeContainerValue))
				{
					tokenReader.LogError($"The type \'{value.GetUserFacingDecl()}\' can not be used as a value in a TMap");
				}

				if (tokenReader.TryOptional(','))
				{
					UhtToken allocatorToken = tokenReader.GetIdentifier();
					if (allocatorToken.IsIdentifier("FMemoryImageSetAllocator"))
					{
						propertySettings.Allocator = UhtPropertyAllocator.MemoryImage;
					}
					else
					{
						tokenReader.LogError($"Found '{allocatorToken.Value}' - explicit allocators are not supported in TMap properties.");
					}
				}
				tokenReader.Require('>');

				//@TODO: Prevent sparse delegate types from being used in a container

				return new UhtMapProperty(propertySettings, key, value);
			}
		}
		#endregion
	}
}
