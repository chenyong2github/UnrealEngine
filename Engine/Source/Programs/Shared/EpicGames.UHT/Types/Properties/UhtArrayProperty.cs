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

	/// <summary>
	/// Represents the FArrayProperty engine type
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "ArrayProperty", IsProperty = true)]
	public class UhtArrayProperty : UhtContainerBaseProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "ArrayProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "TArray"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "TARRAY"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		/// <summary>
		/// Construct a new array property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		/// <param name="Value">Inner property value</param>
		public UhtArrayProperty(UhtPropertySettings PropertySettings, UhtProperty Value) : base(PropertySettings, Value)
		{
			// If the creation of the value property set more flags, then copy those flags to ourselves
			this.PropertyFlags |= Value.PropertyFlags & EPropertyFlags.UObjectWrapper;

			if (Value.MetaData.ContainsKey(UhtNames.NativeConst))
			{
				this.MetaData.Add(UhtNames.NativeConstTemplateArg, "");
				Value.MetaData.Remove(UhtNames.NativeConst);
			}

			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef;
			this.PropertyCaps &= ~(UhtPropertyCaps.CanBeContainerValue | UhtPropertyCaps.CanBeContainerKey);
			this.PropertyCaps = (this.PropertyCaps & ~(UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn)) |
				(this.ValueProperty.PropertyCaps & (UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn));
			this.ValueProperty.SourceName = this.SourceName;
			this.ValueProperty.EngineName = this.EngineName;
			this.ValueProperty.PropertyFlags = this.PropertyFlags & EPropertyFlags.PropagateToArrayInner;
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
					Builder.Append("TArray");
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.AppendFunctionThunkParameterArrayType(this.ValueProperty);
					break;

				default:
					Builder.Append("TArray<").AppendPropertyText(this.ValueProperty, TextType, true);
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
			Builder.AppendMemberDecl(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_Inner"), Tabs);
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FArrayPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			Builder.AppendMemberDef(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_Inner"), "0", Tabs);
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FArrayPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Array");
			Builder.Append(this.Allocator == UhtPropertyAllocator.MemoryImage ? "EArrayPropertyFlags::UsesMemoryImageAllocator" : "EArrayPropertyFlags::None").Append(", ");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberPtr(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			Builder.AppendMemberPtr(this.ValueProperty, Context, Name, GetNameSuffix(NameSuffix, "_Inner"), Tabs);
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
		protected override void ValidateFunctionArgument(UhtFunction Function, UhtValidationOptions Options)
		{
			base.ValidateFunctionArgument(Function, Options);

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (!Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest))
				{
					if (this.RefQualifier != UhtPropertyRefQualifier.ConstRef && !this.bIsStaticArray)
					{
						this.LogError("Replicated TArray parameters must be passed by const reference");
					}
				}
			}
		}

		/// <inheritdoc/>
		public override void Validate(UhtStruct OuterStruct, UhtProperty OutermostProperty, UhtValidationOptions Options)
		{
			base.Validate(OuterStruct, OutermostProperty, Options);

			if (this.Allocator != UhtPropertyAllocator.Default)
			{
				if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
				{
					this.LogError("Replicated arrays with MemoryImageAllocators are not yet supported");
				}
			}
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtArrayProperty OtherArray)
			{
				return this.ValueProperty.IsSameType(OtherArray.ValueProperty);
			}
			return false;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			ParameterFlags |= UhtRigVMParameterFlags.IsArray;
			StringBuilder Builder = new StringBuilder();
			Builder.AppendPropertyText(this, UhtPropertyTextType.RigVMTemplateArg);
			return Builder.ToString();
		}

#region Keyword
		[UhtPropertyType(Keyword = "TArray")]
		private static UhtProperty? ArrayProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			using (var TokenContext = new UhtMessageContext(TokenReader, "TArray"))
			{
				if (!TokenReader.SkipExpectedType(MatchedToken.Value, PropertySettings.PropertyCategory == UhtPropertyCategory.Member))
				{
					return null;
				}
				TokenReader.Require('<');

				// Parse the value type
				UhtProperty? Value = UhtPropertyParser.ParseTemplateParam(ResolvePhase, PropertySettings, PropertySettings.SourceName, TokenReader);
				if (Value == null)
				{
					return null;
				}

				if (!Value.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeContainerValue))
				{
					TokenReader.LogError($"The type \'{Value.GetUserFacingDecl()}\' can not be used as a value in a TArray");
				}

				if (TokenReader.TryOptional(','))
				{
					// If we found a comma, read the next thing, assume it's an allocator, and report that
					UhtToken AllocatorToken = TokenReader.GetIdentifier();
					if (AllocatorToken.IsIdentifier("FMemoryImageAllocator"))
					{
						PropertySettings.Allocator = UhtPropertyAllocator.MemoryImage;
					}
					else if (AllocatorToken.IsIdentifier("TMemoryImageAllocator"))
					{
						TokenReader.RequireList('<', '>', "TMemoryImageAllocator template arguments");
						PropertySettings.Allocator = UhtPropertyAllocator.MemoryImage;
					}
					else
					{
						throw new UhtException(TokenReader, $"Found '{AllocatorToken.Value.ToString()}' - explicit allocators are not supported in TArray properties.");
					}
				}
				TokenReader.Require('>');

				//@TODO: Prevent sparse delegate types from being used in a container

				return new UhtArrayProperty(PropertySettings, Value);
			}
		}
#endregion
	}
}
