// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorViewportViewModel.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorPropertyUtils.h"

FDisplayClusterConfiguratorViewportViewModel::FDisplayClusterConfiguratorViewportViewModel(UDisplayClusterConfigurationViewport* Viewport)
{
	ViewportPtr = Viewport;

	INIT_PROPERTY_HANDLE(UDisplayClusterConfigurationViewport, Viewport, Region);
}

void FDisplayClusterConfiguratorViewportViewModel::SetRegion(const FDisplayClusterConfigurationRectangle& NewRegion)
{
	UDisplayClusterConfigurationViewport* Viewport = ViewportPtr.Get();
	check(Viewport);
	Viewport->Modify();

	const FDisplayClusterConfigurationRectangle& CurrentRegion = Viewport->Region;
	bool bViewportChanged = false;

	if (CurrentRegion.X != NewRegion.X)
	{
		bViewportChanged = true;

		TSharedPtr<IPropertyHandle> XHandle = RegionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, X));
		check(XHandle);

		XHandle->SetValue(NewRegion.X, EPropertyValueSetFlags::NotTransactable);
	}

	if (CurrentRegion.Y != NewRegion.Y)
	{
		bViewportChanged = true;

		TSharedPtr<IPropertyHandle> YHandle = RegionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, Y));
		check(YHandle);

		YHandle->SetValue(NewRegion.Y, EPropertyValueSetFlags::NotTransactable);
	}

	if (CurrentRegion.W != NewRegion.W)
	{
		bViewportChanged = true;

		TSharedPtr<IPropertyHandle> WHandle = RegionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, W));
		check(WHandle);

		WHandle->SetValue(NewRegion.W, EPropertyValueSetFlags::NotTransactable);
	}

	if (CurrentRegion.H != NewRegion.H)
	{
		bViewportChanged = true;

		TSharedPtr<IPropertyHandle> HHandle = RegionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, H));
		check(HHandle);

		HHandle->SetValue(NewRegion.H, EPropertyValueSetFlags::NotTransactable);
	}

	if (bViewportChanged)
	{
		Viewport->MarkPackageDirty();
	}
}