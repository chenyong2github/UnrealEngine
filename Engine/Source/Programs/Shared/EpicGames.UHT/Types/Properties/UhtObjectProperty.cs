// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// FObjectProperty
	/// </summary>
	[UhtEngineClass(Name = "ObjectProperty", IsProperty = true)]
	public class UhtObjectProperty : UhtObjectPropertyBase
	{
		/// <inheritdoc/>
		public override string EngineClassName => "ObjectProperty";

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		/// <param name="Class">Referenced class</param>
		/// <param name="MetaClass">Optional reference class (used by class properties)</param>
		/// <param name="ExtraFlags">Extra flags to add to the property</param>
		public UhtObjectProperty(UhtPropertySettings PropertySettings, UhtClass Class, UhtClass? MetaClass = null, EPropertyFlags ExtraFlags = EPropertyFlags.None)
			: base(PropertySettings, Class, MetaClass)
		{
			this.PropertyFlags |= ExtraFlags;
			this.PropertyCaps |= UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.CanBeInstanced | UhtPropertyCaps.CanExposeOnSpawn |
				UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
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
						this.PropertyFlags |= EPropertyFlags.InstancedReference;
						this.MetaData.Add(UhtNames.EditInline, true);
					}
					break;
			}
			return bResults;
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool bDeepScan)
		{
			return this.Class.HierarchyHasAnyClassFlags(EClassFlags.DefaultToInstanced);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.FunctionThunkRetVal:
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
					{
						Builder.Append("const ");
					}
					Builder.Append(this.Class.SourceName).Append("*");
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					Builder.Append(this.Class.SourceName);
					break;

				default:
					Builder.Append(this.Class.SourceName).Append("*");
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FObjectPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FObjectPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Object");
			AppendMemberDefRef(Builder, Context, this.Class, false);
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
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtObjectProperty OtherObject)
			{
				return this.Class == OtherObject.Class && this.MetaClass == OtherObject.MetaClass;
			}
			return false;
		}
	}
}
