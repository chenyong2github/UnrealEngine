// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"

BEGIN_NAMESPACE_UE_AC

class IConnectionListener
{
  public:
	class FConnection
	{
		FString Source;
		FString Destination;
	};

	virtual ~IConnectionListener(){};

	virtual void ConnectionsChanged(const TSharedRef< TArray< FConnection > >& FConnection) = 0;

	static void RegisterListener(IConnectionListener* InListener);

	static void UnregisterListener(IConnectionListener* InListener);
};

class FConnectionDialog;

class FConnectionWindow : IConnectionListener
{
  public:
	static void Create();
	static void Delete();

	virtual void ConnectionsChanged(const TSharedRef< TArray< FConnection > >& FConnection) override;

  private:
	FConnectionWindow();

	~FConnectionWindow();

	void Start();

	void Stop();

	FConnectionDialog* ConnectionDialog = nullptr;

	TSharedPtr< TArray< FConnection > > Connections;

	// Control access on this object
	mutable GS::Lock AccessControl;

	// Condition variable
	GS::Condition AccessCondition;
};

END_NAMESPACE_UE_AC
