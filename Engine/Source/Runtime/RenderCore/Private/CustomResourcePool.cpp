
#include "CustomResourcePool.h"

ICustomResourcePool* GCustomResourcePool = nullptr;

void ICustomResourcePool::TickPoolElements()
{
	if (GCustomResourcePool)
	{
		GCustomResourcePool->Tick();
	}
}
