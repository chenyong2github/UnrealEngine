// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "SoftClassProperty", IsProperty = true)]
	public class UhtSoftClassProperty : UhtSoftObjectProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "SoftClassProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "SoftClassPtr"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "SOFTCLASS"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		public UhtSoftClassProperty(UhtPropertySettings PropertySettings, UhtClass PropertyClass, UhtClass MetaClass)
			: base(PropertySettings, PropertyClass, MetaClass)
		{
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return this.MetaClass != null ? $"class {this.MetaClass.SourceName};" : null;
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			Collector.AddCrossModuleReference(this.MetaClass, false);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				default:
					Builder.Append("TSoftClassPtr<").Append(this.MetaClass?.SourceName).Append("> ");
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FSoftClassPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FSoftClassPropertyParams", "UECodeGen_Private::EPropertyGenFlags::SoftClass");
			AppendMemberDefRef(Builder, Context, this.MetaClass, false);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return $"TSoftClassPtr<{this.MetaClass?.SourceName}>";
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TSoftClassPtr")]
		public static UhtProperty? SoftClassPtrProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtClass? MetaClass = ParseTemplateClass(PropertySettings, TokenReader, MatchedToken);
			if (MetaClass == null)
			{
				return null;
			}

			// With TSubclassOf, MetaClass is used as a class limiter.  
			return new UhtSoftClassProperty(PropertySettings, MetaClass.Session.UClass, MetaClass);
		}
		#endregion
	}
}
