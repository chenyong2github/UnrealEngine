// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "InterfaceProperty", IsProperty = true)]
	public class UhtInterfaceProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "InterfaceProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "TScriptInterface"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "TINTERFACE"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtClass>))]
		public UhtClass InterfaceClass { get; set; }

		public UhtInterfaceProperty(UhtPropertySettings PropertySettings, UhtClass InterfaceClass) : base(PropertySettings)
		{
			this.InterfaceClass = InterfaceClass;
			this.PropertyFlags |= EPropertyFlags.UObjectWrapper;
			this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.PassCppArgsByRef;
			this.PropertyCaps &= ~(UhtPropertyCaps.CanHaveConfig | UhtPropertyCaps.CanBeContainerKey);
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResults = base.ResolveSelf(Phase);
			switch (Phase)
			{
				case UhtResolvePhase.Final:
					if (this.InterfaceClass.HierarchyHasAnyClassFlags(EClassFlags.DefaultToInstanced))
					{
						this.PropertyFlags |= (EPropertyFlags.InstancedReference | EPropertyFlags.ExportObject) & ~this.DisallowPropertyFlags;
					}
					break;
			}
			return bResults;
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool bDeepScan)
		{
			return !this.DisallowPropertyFlags.HasAnyFlags(EPropertyFlags.InstancedReference) && this.InterfaceClass.HierarchyHasAnyClassFlags(EClassFlags.DefaultToInstanced);
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			Collector.AddCrossModuleReference(this.InterfaceClass, false);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			UhtClass? ExportClass = this.InterfaceClass;
			while (ExportClass != null && !ExportClass.ClassFlags.HasAnyFlags(EClassFlags.Native))
			{
				ExportClass = ExportClass.SuperClass;
			}
			return ExportClass != null ? $"class {ExportClass.SourceName};" : null;
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			yield return this.InterfaceClass;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.SparseShort:
					Builder.Append("TScriptInterface");
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.Append(this.InterfaceClass.SourceName);
					break;

				default:
					Builder.Append("TScriptInterface<").Append(this.InterfaceClass.SourceName).Append('>');
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FInterfacePropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			// FScriptInterface<USomeInterface> is valid so in that case we need to pass in the interface class and not the alternate object (which in the end is the same object)
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FInterfacePropertyParams", "UECodeGen_Private::EPropertyGenFlags::Interface");
			AppendMemberDefRef(Builder, Context, this.InterfaceClass.AlternateObject != null ? this.InterfaceClass.AlternateObject : this.InterfaceClass, false);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("NULL");
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			return false;
		}

		/// <inheritdoc/>
		protected override void ValidateMember(UhtStruct Struct, UhtValidationOptions Options)
		{
			base.ValidateMember(Struct, Options);

			if (this.PointerType == UhtPointerType.Native)
			{
				this.LogError($"UPROPERTY pointers cannot be interfaces - did you mean TScriptInterface<{this.InterfaceClass.SourceName}>?");
			}
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtInterfaceProperty OtherObject)
			{
				return this.InterfaceClass == OtherObject.InterfaceClass;
			}
			return false;
		}

		/// <inheritdoc/>
		public override bool MustBeConstArgument([NotNullWhen(true)] out UhtType? ErrorType)
		{
			ErrorType = this.InterfaceClass;
			return this.InterfaceClass.ClassFlags.HasAnyFlags(EClassFlags.Const);
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return null;
		}

		#region Keywords
		[UhtPropertyType(Keyword = "FScriptInterface", Options = UhtPropertyTypeOptions.Simple)] // This can't be immediate due to the reference to UInterface
		public static UhtProperty? FScriptInterfaceProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			return new UhtInterfaceProperty(PropertySettings, PropertySettings.Outer.Session.IInterface);
		}

		[UhtPropertyType(Keyword = "TScriptInterface")]
		public static UhtProperty? TScriptInterfaceProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtClass? PropertyClass = UhtObjectPropertyBase.ParseTemplateObject(PropertySettings, TokenReader, MatchedToken, false);
			if (PropertyClass == null)
			{
				return null;
			}

			if (PropertyClass.ClassFlags.HasAnyFlags(EClassFlags.Interface))
			{
				return new UhtInterfaceProperty(PropertySettings, PropertyClass);
			}
			return new UhtObjectProperty(PropertySettings, PropertyClass, EPropertyFlags.UObjectWrapper);
		}
		#endregion
	}
}
