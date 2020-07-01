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

