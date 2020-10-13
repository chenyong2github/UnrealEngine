// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOpenXRARTrackedGeometryHolder.h"
#include "MRMeshBufferDefines.h"
#include "MRMeshComponent.h"

UARTrackedGeometry* FOpenXRQRCodeData::ConstructNewTrackedGeometry(TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface)
{
	check(IsInGameThread());

	UARTrackedQRCode* NewQRCode = NewObject<UARTrackedQRCode>();
	NewQRCode->UniqueId = Id;
	return NewQRCode;
};

void FOpenXRQRCodeData::UpdateTrackedGeometry(UARTrackedGeometry* TrackedGeometry, TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface)
{
	check(IsInGameThread());

	UARTrackedQRCode* UpdatedQRCode = Cast<UARTrackedQRCode>(TrackedGeometry);
	check(UpdatedQRCode != nullptr);

	UpdatedQRCode->UpdateTrackedGeometry(ARSupportInterface.ToSharedRef(),
		GFrameCounter,
		Timestamp,
		LocalToTrackingTransform,
		ARSupportInterface->GetAlignmentTransform(),
		Size,
		QRCode,
		Version);
	UpdatedQRCode->SetTrackingState(TrackingState);
}


UARTrackedGeometry* FOpenXRMeshUpdate::ConstructNewTrackedGeometry(TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface)
{
	check(IsInGameThread());

	UARTrackedGeometry* NewMesh = NewObject<UARTrackedGeometry>();
	NewMesh->UniqueId = Id;
	return NewMesh;
};

void FOpenXRMeshUpdate::UpdateTrackedGeometry(UARTrackedGeometry* TrackedGeometry, TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface)
{
	check(IsInGameThread());

	UARTrackedGeometry* UpdatedGeometry = Cast<UARTrackedGeometry>(TrackedGeometry);
	check(UpdatedGeometry != nullptr);

	if (Vertices.Num() > 0)
	{
		// Update MRMesh if it's available
		if (auto MRMesh = UpdatedGeometry->GetUnderlyingMesh())
		{
			// MRMesh takes ownership of the data in the arrays at this point
			MRMesh->UpdateMesh(LocalToTrackingTransform.GetLocation(), LocalToTrackingTransform.GetRotation(), LocalToTrackingTransform.GetScale3D(), Vertices, Indices);
		}
	}

	// Update the tracking data, it MUST be done after UpdateMesh
	UpdatedGeometry->UpdateTrackedGeometry(ARSupportInterface.ToSharedRef(),
		GFrameCounter,
		FPlatformTime::Seconds(),
		LocalToTrackingTransform,
		ARSupportInterface->GetAlignmentTransform());
	// Mark this as a world mesh that isn't recognized as a particular scene type, since it is loose triangles
	UpdatedGeometry->SetObjectClassification(Type);
	UpdatedGeometry->SetTrackingState(TrackingState);

	check(false); // Meshes are currently handled with a unique codepath.
}