// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableTransformLayout.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "InputCoreTypes.h"
#include "PropertyRestriction.h"
#include "SCurveEditor.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "WaveTableBank.h"
#include "WaveTableFileUtilities.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "WaveTable"


namespace WaveTable
{
	namespace Editor
	{
		void FTransformLayoutCustomizationBase::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
		{
		}

		void FTransformLayoutCustomizationBase::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
		{
			uint32 NumChildren;
			StructPropertyHandle->GetNumChildren(NumChildren);

			FProperty* Property = nullptr;
			StructPropertyHandle->GetValue(Property);

			TMap<FName, TSharedPtr<IPropertyHandle>> PropertyHandles;

			for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
			{
				TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
				const FName PropertyName = ChildHandle->GetProperty()->GetFName();
				PropertyHandles.Add(PropertyName, ChildHandle);
			}

			CurveHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FWaveTableTransform, Curve)).ToSharedRef();
			ChildBuilder.AddProperty(CurveHandle->AsShared());

			TSharedRef<IPropertyHandle> ScalarHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FWaveTableTransform, Scalar)).ToSharedRef();

			ChildBuilder.AddProperty(ScalarHandle)
				.EditCondition(TAttribute<bool>::Create([this]() { return IsScaleableCurve(); }), nullptr)
				.Visibility(TAttribute<EVisibility>::Create([this]() { return IsScaleableCurve() ? EVisibility::Visible : EVisibility::Hidden; }));

			TSharedRef<IPropertyHandle> SharedCurveHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FWaveTableTransform, CurveShared)).ToSharedRef();
			ChildBuilder.AddProperty(SharedCurveHandle)
				.EditCondition(TAttribute<bool>::Create([this]() { return GetCurve() == EWaveTableCurve::Shared; }), nullptr)
				.Visibility(TAttribute<EVisibility>::Create([this]() { return GetCurve() == EWaveTableCurve::Shared ? EVisibility::Visible : EVisibility::Hidden; }));

			TAttribute<EVisibility> IsWaveTableVisibilityAttribute = TAttribute<EVisibility>::Create([this]()
			{
				return GetCurve() == EWaveTableCurve::File ? EVisibility::Visible : EVisibility::Hidden;
			});

			WaveTableOptionsHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FWaveTableTransform, WaveTableSettings)).ToSharedRef();

			uint32 NumWaveTableOptions = 0;
			if (WaveTableOptionsHandle->GetNumChildren(NumWaveTableOptions))
			{
				for (int32 i = 0; i < static_cast<int32>(NumWaveTableOptions); ++i)
				{
					TSharedPtr<IPropertyHandle> ChildProperty = WaveTableOptionsHandle->GetChildHandle(i);
					if (ChildProperty->GetProperty()->GetFName() != GET_MEMBER_NAME_CHECKED(FWaveTableSettings, FilePath))
					{
						ChildProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]
						{
							RefreshWaveTable();
						}));
					}
				}

				ChannelIndexHandle = WaveTableOptionsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaveTableSettings, ChannelIndex));

				FilePathHandle = WaveTableOptionsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWaveTableSettings, FilePath));
				FilePathHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda([this] { CachePCMFromFile(); }));
				FilePathHandle = FilePathHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFilePath, FilePath));
			}

			ChildBuilder.AddProperty(WaveTableOptionsHandle->AsShared())
				.EditCondition(TAttribute<bool>::Create([this]() { return GetCurve() == EWaveTableCurve::File; }), nullptr)
				.Visibility(IsWaveTableVisibilityAttribute);
		}

		void FTransformLayoutCustomizationBase::CachePCMFromFile()
		{
			if (FWaveTableTransform* Transform = GetTransform())
			{
				bool bLoadSucceeded = false;
				FString FilePath;
				if (ensure(FilePathHandle->GetValue(FilePath) == FPropertyAccess::Success))
				{
					int32 ChannelIndex = 0;
					if (ensure(ChannelIndexHandle->GetValue(ChannelIndex) == FPropertyAccess::Success))
					{
						TArray<float>& SourcePCMData = Transform->WaveTableSettings.SourcePCMData;
						WaveTable::Editor::FileUtilities::LoadPCMChannel(FilePath, ChannelIndex, SourcePCMData);
						bLoadSucceeded = true;
					}
				}

				if (!bLoadSucceeded)
				{
					Transform->WaveTableSettings.SourcePCMData.Empty();
				}
			}

			RefreshWaveTable();
		}

		void FTransformLayoutCustomizationBase::RefreshWaveTable() const
		{
			if (FWaveTableTransform* Transform = GetTransform())
			{
				if (const EWaveTableResolution* Resolution = GetResolution())
				{
					const bool bBipolar = IsBipolar();
					Transform->CacheWaveTable(*Resolution, bBipolar);
				}
			}
		}

		bool FTransformLayoutCustomizationBase::IsScaleableCurve() const
		{
			static const TArray<EWaveTableCurve> ScalarFilters = { EWaveTableCurve::Exp, EWaveTableCurve::Exp_Inverse, EWaveTableCurve::Log };
			return ScalarFilters.Contains(GetCurve());
		}

		EWaveTableCurve FTransformLayoutCustomizationBase::GetCurve() const
		{
			if (!CurveHandle.IsValid())
			{
				return EWaveTableCurve::Linear;
			}

			uint8 CurveValue = static_cast<uint8>(EWaveTableCurve::Linear);
			CurveHandle->GetValue(CurveValue);
			return static_cast<EWaveTableCurve>(CurveValue);
		}

		int32 FTransformLayoutCustomizationBase::GetOwningArrayIndex() const
		{
			int32 TableIndex = INDEX_NONE;
			if (ensure(WaveTableOptionsHandle.IsValid()))
			{
				TSharedPtr<IPropertyHandleArray> ParentArray;
				TSharedPtr<IPropertyHandle> ChildHandle = WaveTableOptionsHandle;
				while (ChildHandle.IsValid() && TableIndex == INDEX_NONE)
				{
					TableIndex = ChildHandle->GetIndexInArray();
					ChildHandle = ChildHandle->GetParentHandle();
				}
			}

			return TableIndex;
		}

		const EWaveTableResolution* FTransformLayoutCustomization::GetResolution() const
		{
			if (ensure(WaveTableOptionsHandle.IsValid()))
			{
				TArray<UObject*> OuterObjects;
				WaveTableOptionsHandle->GetOuterObjects(OuterObjects);
				if (OuterObjects.Num() == 1)
				{
					if (UWaveTableBank* Bank = Cast<UWaveTableBank>(OuterObjects.Last()))
					{
						return &Bank->Resolution;
					}
				}
			}

			return nullptr;
		}

		bool FTransformLayoutCustomization::IsBipolar() const
		{
			if (ensure(WaveTableOptionsHandle.IsValid()))
			{
				TArray<UObject*> OuterObjects;
				WaveTableOptionsHandle->GetOuterObjects(OuterObjects);
				if (OuterObjects.Num() == 1)
				{
					if (UWaveTableBank* Bank = Cast<UWaveTableBank>(OuterObjects.Last()))
					{
						return Bank->bBipolar;
					}
				}
			}

			return false;
		}

		FWaveTableTransform* FTransformLayoutCustomization::GetTransform() const
		{
			if (ensure(WaveTableOptionsHandle.IsValid()))
			{
				TArray<UObject*> OuterObjects;
				WaveTableOptionsHandle->GetOuterObjects(OuterObjects);
				if (OuterObjects.Num() == 1)
				{
					if (UWaveTableBank* Bank = Cast<UWaveTableBank>(OuterObjects.Last()))
					{
						const int32 TableIndex = GetOwningArrayIndex();
						if (TableIndex != INDEX_NONE)
						{
							if (TableIndex < Bank->Entries.Num())
							{
								return &Bank->Entries[TableIndex].Transform;
							}
						}
					}
				}
			}

			return nullptr;
		}
	} // namespace Editor
} // namespace WaveTable
#undef LOCTEXT_NAMESPACE // AudioModulation
