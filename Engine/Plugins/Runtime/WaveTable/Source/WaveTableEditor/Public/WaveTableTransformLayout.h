// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Curves/SimpleCurve.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "PropertyHandle.h"
#include "SCurveEditor.h"
#include "WaveTableSettings.h"
#include "WaveTableTransform.h"


namespace WaveTable
{
	namespace Editor
	{
		class WAVETABLEEDITOR_API FTransformLayoutCustomizationBase : public IPropertyTypeCustomization
		{
		public:
			//~ Begin IPropertyTypeCustomization
			virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			//~ End IPropertyTypeCustomization

		protected:
			EWaveTableCurve GetCurve() const;

			virtual const EWaveTableResolution* GetResolution() const = 0;
			virtual FWaveTableTransform* GetTransform() const = 0;
			virtual bool IsBipolar() const = 0;

			void CachePCMFromFile();
			int32 GetOwningArrayIndex() const;

			bool IsScaleableCurve() const;
			void RefreshWaveTable() const;

			TSharedPtr<IPropertyHandle> CurveHandle;
			TSharedPtr<IPropertyHandle> ChannelIndexHandle;
			TSharedPtr<IPropertyHandle> FilePathHandle;
			TSharedPtr<IPropertyHandle> WaveTableOptionsHandle;
		};

		class WAVETABLEEDITOR_API FTransformLayoutCustomization : public FTransformLayoutCustomizationBase
		{
		public:
			static TSharedRef<IPropertyTypeCustomization> MakeInstance()
			{
				return MakeShared<FTransformLayoutCustomization>();
			}

			virtual const EWaveTableResolution* GetResolution() const override;
			virtual bool IsBipolar() const override;
			virtual FWaveTableTransform* GetTransform() const override;
		};
	} // namespace Editor
} // namespace WaveTable
