// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/VCamWidget.h"

void UVCamWidget::InitializeConnections(UVCamComponent* VCam)
{
	if (!IsValid(VCam))
	{
		return;
	}

	// Iteratively call AttemptConnection on each connection within the widget and notify the result via OnConnectionUpdated
	for (TPair<FName, FVCamConnection>& Connection : Connections)
	{
		const FName& ConnectionName = Connection.Key;
		FVCamConnection& VCamConnection = Connection.Value;

		const bool bDidConnectSuccessfully = VCamConnection.AttemptConnection(VCam);

		if (!bDidConnectSuccessfully)
		{
			UE_LOG(LogVCamConnection, Warning, TEXT("Widget %s: Failed to create for VCam Connection with Connection Name: %s"), *GetName(), *ConnectionName.ToString());	
		}
		
		OnConnectionUpdated(ConnectionName, bDidConnectSuccessfully, VCamConnection.ConnectionTargetSettings.TargetConnectionPoint, VCamConnection.ConnectedModifier, VCamConnection.ConnectedAction);
	}
}
