// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/InvalidateWidgetReason.h"

#include <type_traits>

class FSlateWidgetClassData;


class SLATECORE_API FSlateAttributeDescriptor
{
public:
	using OffsetType = uint32; //UPTRINT

	static uint32 DefaultSortOrder(OffsetType Offset) { return Offset * 10; }

	struct FAttribute
	{
		FName Name;
		OffsetType Offset;
		FName Prerequisite;
		FName Dependency;
		uint32 SortOrder;
		TAttribute<EInvalidateWidgetReason> InvalidationReason;
		bool bIsMemberAttribute;
	};

	struct SLATECORE_API FInitializer
	{
	private:
		friend FSlateWidgetClassData;
		FInitializer(FSlateAttributeDescriptor& InDescriptor);
		FInitializer(FSlateAttributeDescriptor& InDescriptor, const FSlateAttributeDescriptor& ParentDescriptor);
		FInitializer(const FInitializer&) = delete;
		FInitializer& operator=(const FInitializer&) = delete;

	public:
		~FInitializer();

		struct SLATECORE_API FAttributeEntry
		{
			FAttributeEntry(FSlateAttributeDescriptor& Descriptor, int32 InAttributeIndex);

			/** Assign an order in which the attributes should be updated. */
			FAttributeEntry& SetPrerequisite(FName Prerequisite);
			/** Update the property every. */
			FAttributeEntry& UpdateEveryFrame();
			/** The property only need to be poll when the dependency changes. */
			FAttributeEntry& UpdateDependency(FName Dependency);

		private:
			FSlateAttributeDescriptor& Descriptor;
			int32 AttributeIndex;
		};

		FAttributeEntry AddMemberAttribute(FName AttributeName, OffsetType Offset);

		FAttributeEntry AddMemberAttribute(FName AttributeName, OffsetType Offset, TAttribute<EInvalidateWidgetReason> ReasonGetter);

		void OverrideInvalidationReason(FName AttributeName, TAttribute<EInvalidateWidgetReason> Reason);

		void SetPrerequisite(FName AttributeName, FName Prerequisite);

	private:
		FSlateAttributeDescriptor& Descriptor;
	};

	FAttribute const* FindAttribute(FName AttributeName) const;

	FAttribute const* FindMemberAttribute(OffsetType AttributeOffset) const;

private:
	FAttribute* FindAttribute(FName AttributeName);

	FInitializer::FAttributeEntry AddMemberAttribute(FName AttributeName, OffsetType Offset, TAttribute<EInvalidateWidgetReason>&& ReasonGetter);
	void OverrideInvalidationReason(FName AttributeName, TAttribute<EInvalidateWidgetReason>&& ReasonGetter);
	void SetDependency(FAttribute& Attribute, FName Dependency);
	void SetPrerequisite(FAttribute& Attribute, FName Prerequisite);

private:
	TArray<FAttribute> Attributes;
};

#define SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(_Initializer, _Property) {_Initializer.AddMemberAttribute(GET_MEMBER_NAME_CHECKED(PrivateThisType, _Property), STRUCT_OFFSET(PrivateThisType, _Property));}
#define SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION_REASON(_Initializer, _Property, _Reason) {_Initializer.AddMemberAttribute(GET_MEMBER_NAME_CHECKED(PrivateThisType, _Property), STRUCT_OFFSET(PrivateThisType, _Property), _Reason));}

using FSlateAttributeInitializer = FSlateAttributeDescriptor::FInitializer;
