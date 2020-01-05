// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "CoreMinimal.h"

#include "LiveLinkTypes.h"
#include "LiveLinkVirtualSubject.h"
#include "Types/SlateEnums.h"
#include "Widgets/Views/SListView.h"


class ILiveLinkClient;
class IPropertyHandle;
class ITableRow;
class STableViewBase;


/**
* Customizes a ULiveLinkVirtualSubjectDetails
*/
class FLiveLinkVirtualSubjectDetailCustomization : public IDetailCustomization
{
public:

	// Data type of out subject tree UI
	typedef TSharedPtr<FName> FSubjectEntryPtr;

	static TSharedRef<IDetailCustomization> MakeInstance() 
	{
		return MakeShared<FLiveLinkVirtualSubjectDetailCustomization>();
	}

	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
	// End IDetailCustomization interface

private:
	
	// Creates subject tree entry widget
	TSharedRef<ITableRow> OnGenerateWidgetForSubjectItem(FSubjectEntryPtr InItem, const TSharedRef<STableViewBase>& OwnerTable);

	// The tile set being edited
	TWeakObjectPtr<ULiveLinkVirtualSubject> SubjectPtr;

	ILiveLinkClient* Client;

	// Cached reference to our details builder so we can force refresh
	IDetailLayoutBuilder* MyDetailsBuilder;

	// Cached property pointers
	TSharedPtr<IPropertyHandle> SubjectsPropertyHandle;

	// Cached data for the subject tree UI
	TArray<FSubjectEntryPtr> SubjectsListItems;
	TSharedPtr< SListView< FSubjectEntryPtr > > SubjectsListView;
};
