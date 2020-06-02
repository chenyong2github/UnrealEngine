#include "ActorDetailsExtensionContext.h"

const TArray<TWeakObjectPtr<UObject>>& UActorDetailsExtensionContext::GetSelectedObjects() const
{
	if (!GetSelectedObjectsDelegate.IsBound())
	{
		static const TArray<TWeakObjectPtr<UObject>> EmptySelection;
		return EmptySelection;
	}
	return GetSelectedObjectsDelegate.Execute();
}
