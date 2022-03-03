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
	[UhtEngineClass(Name = "ObjectPropertyBase", IsProperty = true)]
	public abstract class UhtObjectPropertyBase : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "ObjectPropertyBase"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "Object"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "OBJECT"; }

		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtClass>))]
		public UhtClass Class { get; set; }

		[JsonConverter(typeof(UhtNullableTypeSourceNameJsonConverter<UhtClass>))]
		public UhtClass? MetaClass { get; set; }

		public UhtObjectPropertyBase(UhtPropertySettings PropertySettings, UhtClass PropertyClass, UhtClass? MetaClass) : base(PropertySettings)
		{
			this.Class = PropertyClass;
			this.MetaClass = MetaClass;

			// This applies to EVERYTHING including raw pointer
			// Imply const if it's a parameter that is a pointer to a const class
			// NOTE: We shouldn't be automatically adding const param because in some cases with functions and blueprint native event, the 
			// generated code won't match.  For now, just disabled the auto add in that case and check for the error in the validation code.
			// Otherwise, the user is not warned and they will get compile errors.
			if (PropertySettings.PropertyCategory != UhtPropertyCategory.Member && this.Class.ClassFlags.HasAnyFlags(EClassFlags.Const) && !PropertySettings.Options.HasAnyFlags(UhtPropertyOptions.NoAutoConst))
			{
				this.PropertyFlags |= EPropertyFlags.ConstParm;
			}

			this.PropertyCaps &= ~(UhtPropertyCaps.CanHaveConfig);
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResults = base.ResolveSelf(Phase);
			switch (Phase)
			{
				case UhtResolvePhase.Final:
					if (this.Class.HierarchyHasAnyClassFlags(EClassFlags.DefaultToInstanced))
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
			return !this.DisallowPropertyFlags.HasAnyFlags(EPropertyFlags.InstancedReference) && this.Class.HierarchyHasAnyClassFlags(EClassFlags.DefaultToInstanced);
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			Collector.AddCrossModuleReference(this.Class, false);
			Collector.AddCrossModuleReference(this.MetaClass, false);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return $"class {this.Class.SourceName};";
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			yield return this.Class;
			if (this.MetaClass != null)
			{
				yield return this.MetaClass;
			}
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			int Value;
			if (DefaultValueReader.TryOptional("NULL") ||
				DefaultValueReader.TryOptional("nullptr") ||
				(DefaultValueReader.TryOptionalConstInt(out Value) && Value == 0))
			{
				InnerDefaultValue.Append("None");
				return true;
			}
			return false;
		}

		/// <inheritdoc/>
		public override void ValidateDeprecated()
		{
			ValidateDeprecatedClass(this.Class);
			ValidateDeprecatedClass(this.MetaClass);
		}

		/// <inheritdoc/>
		public override bool MustBeConstArgument([NotNullWhen(true)] out UhtType? ErrorType)
		{
			ErrorType = this.Class;
			return this.Class.ClassFlags.HasAnyFlags(EClassFlags.Const);
		}

		#region Parsing support methods
		public static UhtClass? ParseTemplateObject(UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken, bool bReturnUInterface)
		{
			if (TokenReader.TryOptional("const"))
			{
				PropertySettings.MetaData.Add(UhtNames.NativeConst, "");
			}
			if (!TokenReader.SkipExpectedType(MatchedToken.Value, PropertySettings.PropertyCategory == UhtPropertyCategory.Member))
			{
				return null;
			}
			bool bIsNativeConstTemplateArg = false;
			UhtToken Identifier = new UhtToken();
			TokenReader
				.Require('<')
				.Optional("const", () => { bIsNativeConstTemplateArg = true; })
				.Optional("class")
				.RequireIdentifier((ref UhtToken Token) => { Identifier = Token; })
				.Optional("const", () => { bIsNativeConstTemplateArg = true; })
				.Require('>');

			if (bIsNativeConstTemplateArg)
			{
				PropertySettings.MetaData.Add(UhtNames.NativeConstTemplateArg, "");
			}
			UhtConfig.Instance.RedirectTypeIdentifier(ref Identifier);
			UhtClass? Return = PropertySettings.Outer.FindType(UhtFindOptions.SourceName | UhtFindOptions.Class, ref Identifier, TokenReader) as UhtClass;
			if (Return != null && Return.AlternateObject != null && bReturnUInterface)
			{
				Return = Return.AlternateObject as UhtClass;
			}
			return Return;
		}

		public static UhtClass? ParseTemplateClass(UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtToken Identifier = new UhtToken();

			if (TokenReader.TryOptional("const"))
			{
				PropertySettings.MetaData.Add(UhtNames.NativeConst, "");
			}

			TokenReader.Optional("class");

			if (!TokenReader.SkipExpectedType(MatchedToken.Value, PropertySettings.PropertyCategory == UhtPropertyCategory.Member))
			{
				return null;
			}

			TokenReader
				.Require('<')
				.Optional("class")
				.RequireIdentifier((ref UhtToken Token) => { Identifier = Token; })
				.Require('>');

			UhtConfig.Instance.RedirectTypeIdentifier(ref Identifier);
			UhtClass? Return = PropertySettings.Outer.FindType(UhtFindOptions.SourceName | UhtFindOptions.Class, ref Identifier, TokenReader) as UhtClass;
			if (Return != null && Return.AlternateObject != null)
			{
				Return = Return.AlternateObject as UhtClass;
			}
			return Return;
		}

		public static void ConditionalLogPointerUsage(UhtPropertySettings PropertySettings, UhtPointerMemberBehavior EngineBehavior, UhtPointerMemberBehavior NonEngineBehavior,
			string PointerTypeDesc, IUhtTokenReader TokenReader, int TypeStartPos, string? AlternativeTypeDesc)
		{
			if (PropertySettings.PropertyCategory != UhtPropertyCategory.Member)
			{
				return;
			}

			UhtPackage Package = PropertySettings.Outer.Package;
			bool bEngineBehavior = Package.bIsPartOfEngine && !Package.bIsPlugin;
			UhtPointerMemberBehavior Behavior = bEngineBehavior ? EngineBehavior : NonEngineBehavior;

			if (Behavior == UhtPointerMemberBehavior.AllowSilently)
			{
				return;
			}

			string Type = TokenReader.GetStringView(TypeStartPos, TokenReader.InputPos - TypeStartPos).ToString();
			Type.Replace("\n", " ");
			Type.Replace("\r", "");
			Type.Replace("\t", " ");

			switch (Behavior)
			{
				case UhtPointerMemberBehavior.Disallow:
					if (!string.IsNullOrEmpty(AlternativeTypeDesc))
					{
						TokenReader.LogError($"{PointerTypeDesc} usage in member declaration detected [[[{Type}]]].  This is disallowed for the target/module, consider {AlternativeTypeDesc} as an alternative.");
					}
					else
					{
						TokenReader.LogError($"{PointerTypeDesc} usage in member declaration detected [[[{Type}]]].");
					}
					break;

				case UhtPointerMemberBehavior.AllowAndLog:
					if (!string.IsNullOrEmpty(AlternativeTypeDesc))
					{
						TokenReader.LogTrace($"{PointerTypeDesc} usage in member declaration detected [[[{Type}]]].  Consider {AlternativeTypeDesc} as an alternative.");
					}
					else
					{
						TokenReader.LogTrace("{PointerTypeDesc} usage in member declaration detected [[[{Type}]]].");
					}
					break;

				default:
					throw new UhtIceException("Unknown enum value");
			}
		}
		#endregion
	}
}
