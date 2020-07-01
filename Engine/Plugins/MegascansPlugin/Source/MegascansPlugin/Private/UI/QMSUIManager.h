// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"




class FQMSUIManagerImpl;



class  FQMSUIManager
{
public:
	static void Initialize();
	static void Shutdown();
	static TUniquePtr<FQMSUIManagerImpl> Instance;

//private:
	//static TUniquePtr<FQMSUIManagerImpl> Instance;
};

