// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIFModule.h"

#include "Modules/ModuleManager.h"
#include "UIFPlayerComponent.h"
#include "UIFPresenter.h"
#include "UIFWidget.h"
#include "UObject/Package.h"

namespace UE::UIFramework::Private
{
	static TSubclassOf<UUIFrameworkPresenter> Director;
}

UUIFrameworkWidget* FUIFrameworkModule::AuthorityAttachWidget(UUIFrameworkPlayerComponent* ReplicationOwner, FUIFrameworkParentWidget Parent, UUIFrameworkWidget* Child)
{
	check(Child);
	check(Parent.IsParentValid());

	if (Child->AuthorityGetParent().IsParentValid())
	{
		AuthorityDetachWidgetFromParent(Child);
	}

	UObject* ParentOuter = Parent.IsPlayerComponent() ? Parent.AsPlayerComponent()->GetOuter() : Parent.AsWidget()->GetOuter();
	if (ParentOuter != Child->GetOuter())
	{
		if (Child->GetOuter() == GetTransientPackage())
		{
			// Recursive Rename to change the outer
			// To the operation recursively to child
		}
	}

	bool bDifferentReplicationOwner = ReplicationOwner != Child->OwnerPlayerComponent;

	Child->AuthorityParent = Parent;
	Child->OwnerPlayerComponent = ReplicationOwner;

	if (ReplicationOwner)
	{
		if (Child->AuthorityParent.IsWidget())
		{
			ReplicationOwner->GetWidgetTree().AuthorityAddWidget(Child->AuthorityParent.AsWidget(), Child);
		}
		else
		{
			check(Child->AuthorityParent.IsPlayerComponent());
			ReplicationOwner->GetWidgetTree().AuthorityAddRoot(Child);
		}
	}

	if (bDifferentReplicationOwner)
	{
		AuthoritySetParentReplicationOwnerRecursive(Child);
	}
	return Child;
}

void FUIFrameworkModule::AuthoritySetParentReplicationOwnerRecursive(UUIFrameworkWidget* Widget)
{
	Widget->AuthorityForEachChildren([Widget](UUIFrameworkWidget* Child)
		{
			if (Child != nullptr)
			{
				check(Child->AuthorityGetParent().IsWidget() && Child->AuthorityGetParent().AsWidget() == Widget);
				Child->OwnerPlayerComponent = Widget->OwnerPlayerComponent;
				Child->AuthorityParent = FUIFrameworkParentWidget(Widget);
				AuthoritySetParentReplicationOwnerRecursive(Child);
			}
		});
}

bool FUIFrameworkModule::AuthorityCanWidgetBeAttached(UUIFrameworkPlayerComponent* ReplicationOwner, UUIFrameworkWidget* Parent, UUIFrameworkWidget* Child)
{
	return true;
}

void FUIFrameworkModule::AuthorityDetachWidgetFromParent(UUIFrameworkWidget* Child)
{
	check(Child);
	if (Child->OwnerPlayerComponent)
	{
		Child->OwnerPlayerComponent->GetWidgetTree().AuthorityRemoveWidget(Child);
	}

	if (Child->AuthorityGetParent().IsParentValid())
	{
		if (Child->AuthorityGetParent().IsWidget())
		{
			Child->AuthorityGetParent().AsWidget()->AuthorityRemoveChild(Child);
		}
		else
		{
			check(Child->AuthorityGetParent().IsPlayerComponent());
			Child->AuthorityGetParent().AsPlayerComponent()->AuthorityRemoveChild(Child);
		}
	}
}

void FUIFrameworkModule::SetPresenterClass(TSubclassOf<UUIFrameworkPresenter> InDirector)
{
	UE::UIFramework::Private::Director = InDirector;
}

TSubclassOf<UUIFrameworkPresenter> FUIFrameworkModule::GetPresenterClass()
{
	return UE::UIFramework::Private::Director.Get() ? UE::UIFramework::Private::Director : TSubclassOf<UUIFrameworkPresenter>(UUIFrameworkGameViewportPresenter::StaticClass());
}

IMPLEMENT_MODULE(FUIFrameworkModule, UIFramework)