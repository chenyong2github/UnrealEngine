// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace EpicGames.UHT.Types
{
	public abstract class UhtContainerBaseProperty : UhtProperty
	{

		/// <inheritdoc/>
		public UhtProperty ValueProperty { get; set; }

		public UhtContainerBaseProperty(UhtPropertySettings PropertySettings, UhtProperty Value) : base(PropertySettings)
		{
			this.ValueProperty = Value;
			this.PropertyCaps = (this.PropertyCaps & ~(UhtPropertyCaps.CanBeInstanced | UhtPropertyCaps.CanHaveConfig)) | (this.ValueProperty.PropertyCaps & (UhtPropertyCaps.CanBeInstanced | UhtPropertyCaps.CanHaveConfig));
		}

		/// <inheritdoc/>
		public override void Validate(UhtStruct OuterStruct, UhtProperty OutermostProperty, UhtValidationOptions Options)
		{
			base.Validate(OuterStruct, OutermostProperty, Options);
			this.ValueProperty.Validate(OuterStruct, OutermostProperty, Options | UhtValidationOptions.IsValue);
		}

		protected static void PropagateFlagsFromInnerAndHandlePersistentInstanceMetadata(UhtProperty Container, UhtMetaData? MetaData, UhtProperty Inner)
		{
			// Copy some of the property flags to the container property.
			if (Inner.PropertyFlags.HasAnyFlags(EPropertyFlags.ContainsInstancedReference | EPropertyFlags.InstancedReference))
			{
				Container.PropertyFlags |= EPropertyFlags.ContainsInstancedReference;
				Container.PropertyFlags &= ~(EPropertyFlags.InstancedReference | EPropertyFlags.PersistentInstance); //this was propagated to the inner

				if (MetaData != null && Inner.PropertyFlags.HasAnyFlags(EPropertyFlags.PersistentInstance))
				{
					Inner.MetaData.Add(MetaData);
				}
			}
		}

		protected static EPropertyFlags ResolveAndReturnNewFlags(UhtProperty Child, UhtResolvePhase Phase)
		{
			EPropertyFlags OldFlags = Child.PropertyFlags;
			Child.Resolve(Phase);
			EPropertyFlags NewFlags = Child.PropertyFlags;
			return NewFlags & ~OldFlags;
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool bDeepScan)
		{
			return this.ValueProperty.ScanForInstancedReferenced(bDeepScan);
		}
	}
}
