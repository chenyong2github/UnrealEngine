// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNEDmlOperator.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

enum EAutoPad
{
    NOTSET,
    SAME_UPPER,
    SAME_LOWER,
    VALID
};

static EAutoPad AutoPadFromString(FStringView StringVal) 
{
    if (FCString::Stricmp(StringVal.GetData(), TEXT("NOTSET")) == 0) 
    {
        return EAutoPad::NOTSET;
    }
    else if (FCString::Stricmp(StringVal.GetData(), TEXT("SAME_UPPER")) == 0)
    {
        return EAutoPad::SAME_UPPER;
    }
    else if (FCString::Stricmp(StringVal.GetData(), TEXT("SAME_LOWER")) == 0)
    {
        return EAutoPad::SAME_LOWER;
    }
    else if (FCString::Stricmp(StringVal.GetData(), TEXT("VALID")) == 0)
    {
        return EAutoPad::VALID;
    }
    else
    {
        return EAutoPad::NOTSET;
    }
}

static DmlUtil::FSmallUIntArray KernelPadding(
    TConstArrayView<uint32> InputShape, TConstArrayView<uint32> WindowSize, 
    TConstArrayView<uint32> Dilations, TConstArrayView<uint32> Strides
    )
{
    const uint32 NumSpatialDimensions = InputShape.Num() - NonspatialDimensionCount;
    check(NumSpatialDimensions >= 1);

    DmlUtil::FSmallUIntArray Padding;
    Padding.SetNumUninitialized(NumSpatialDimensions);

    for (uint32 Dim = 0; Dim < NumSpatialDimensions; ++Dim)
    {
        uint32 InputLen = uint32(InputShape[Dim + NonspatialDimensionCount]);
        uint32 StridedOutLen = (InputLen + Strides[Dim] - 1) / Strides[Dim];
        uint32 KernelLen = 1 + (WindowSize[Dim] - 1) * Dilations[Dim];
        uint32 Len = Strides[Dim] * (StridedOutLen - 1) + KernelLen;
        
        Padding[Dim] = (Len <= InputLen) ? 0 : (Len - InputLen);
    }

    return Padding;
}

static void ComputeStartEndPaddings(
    TConstArrayView<uint32> InputShape,
    const NNECore::FAttributeMap& Attributes, 
    DmlUtil::FSmallUIntArray &OutStartPadding, 
    DmlUtil::FSmallUIntArray &OutEndPadding,
    TConstArrayView<uint32> Padding)
{
    const uint32 NumSpatialDimensions = InputShape.Num() - NonspatialDimensionCount;
    check(NumSpatialDimensions >= 1);

    EAutoPad AutoPad = AutoPadFromString(*Attributes.GetValue<FString>(TEXT("auto_pad")));

    if (AutoPad == EAutoPad::NOTSET)
    {
        DmlUtil::FSmallUIntArray Pads;

        Pads.Init(0u, 2 * NumSpatialDimensions);
        check(DmlUtil::GetArrayAttributeNoOverflow(Attributes.GetAttributeValue(TEXT("pads")), Pads, TConstArrayView<uint32>(Pads)))


        for (uint32 Dim = 0; Dim < NumSpatialDimensions; ++Dim)
        {
            OutStartPadding.Add(Pads[Dim]);
            OutEndPadding.Add(Pads[Dim + NumSpatialDimensions]);
        }
    }
    else if (AutoPad == EAutoPad::VALID)
    {
        OutStartPadding.Init(0, NumSpatialDimensions);
        OutEndPadding.Init(0, NumSpatialDimensions);
    }
    else
    {
        OutStartPadding.Init(0, NumSpatialDimensions);
        OutEndPadding.Init(0, NumSpatialDimensions);

        for (uint32 Dim = 0; Dim < NumSpatialDimensions; ++Dim)
        {
            if (AutoPad == EAutoPad::SAME_LOWER)
            {
                OutStartPadding[Dim] = (Padding[Dim] + 1) / 2;
            }
            else
            {
                OutStartPadding[Dim] = Padding[Dim] / 2;
            }

            OutEndPadding[Dim] = Padding[Dim] - OutStartPadding[Dim];
        }
    }
}

}