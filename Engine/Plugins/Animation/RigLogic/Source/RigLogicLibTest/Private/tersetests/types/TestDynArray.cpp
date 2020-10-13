// Copyright Epic Games, Inc. All Rights Reserved.

#include "tersetests/Defs.h"

#include "terse/types/DynArray.h"

#include <pma/PolyAllocator.h>
#include <pma/resources/AlignedMemoryResource.h>


TEST(DynArrayTest, CreateEmpty) {
    terse::DynArray<int, pma::PolyAllocator<int> > arr;
    ASSERT_TRUE(arr.empty());
    ASSERT_EQ(arr.size(), 0ul);
    ASSERT_EQ(arr.data(), nullptr);
}

TEST(DynArrayTest, CreateUninitialized) {
    terse::DynArray<int, pma::PolyAllocator<int> > arr{10ul};
    ASSERT_FALSE(arr.empty());
    ASSERT_EQ(arr.size(), 10ul);
    ASSERT_NE(arr.data(), nullptr);
}

TEST(DynArrayTest, CreateInitialized) {
    terse::DynArray<int, pma::PolyAllocator<int> > arr{10ul, 2};
    ASSERT_FALSE(arr.empty());
    ASSERT_EQ(arr.size(), 10ul);
    ASSERT_NE(arr.data(), nullptr);

    for (std::size_t i{}; i < arr.size(); ++i) {
        ASSERT_EQ(arr[i], 2);
    }
}

TEST(DynArrayTest, CreateFromRange) {
    int values[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    terse::DynArray<int, pma::PolyAllocator<int> > arr{values, values + 10ul};
    ASSERT_FALSE(arr.empty());
    ASSERT_EQ(arr.size(), 10ul);
    ASSERT_NE(arr.data(), nullptr);
    ASSERT_NE(arr.data(), values);

    for (std::size_t i{}; i < arr.size(); ++i) {
        ASSERT_EQ(arr[i], values[i]);
    }
}

TEST(DynArrayTest, CreateFromPointerSize) {
    int values[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    terse::DynArray<int, pma::PolyAllocator<int> > arr{values, 10ul};
    ASSERT_FALSE(arr.empty());
    ASSERT_EQ(arr.size(), 10ul);
    ASSERT_NE(arr.data(), nullptr);
    ASSERT_NE(arr.data(), values);

    for (std::size_t i{}; i < arr.size(); ++i) {
        ASSERT_EQ(arr[i], values[i]);
    }
}

TEST(DynArrayTest, CopyConstruct) {
    int values[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    terse::DynArray<int, pma::PolyAllocator<int> > arr{values, 10ul};
    terse::DynArray<int, pma::PolyAllocator<int> > arrCopy = arr;

    ASSERT_EQ(arr.size(), 10ul);
    ASSERT_EQ(arr.size(), arrCopy.size());
    for (std::size_t i{}; i < arr.size(); ++i) {
        ASSERT_EQ(arr[i], values[i]);
        ASSERT_EQ(arr[i], arrCopy[i]);
    }

    ASSERT_NE(arr.data(), arrCopy.data());
}

TEST(DynArrayTest, CopyAssign) {
    int values[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    terse::DynArray<int, pma::PolyAllocator<int> > arr{values, 10ul};
    terse::DynArray<int, pma::PolyAllocator<int> > arrCopy;
    arrCopy = arr;

    ASSERT_EQ(arr.size(), 10ul);
    ASSERT_EQ(arr.size(), arrCopy.size());
    for (std::size_t i{}; i < arr.size(); ++i) {
        ASSERT_EQ(arr[i], values[i]);
        ASSERT_EQ(arr[i], arrCopy[i]);
    }

    ASSERT_NE(arr.data(), arrCopy.data());
}

TEST(DynArrayTest, MoveConstruct) {
    int values[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    terse::DynArray<int, pma::PolyAllocator<int> > arr{values, 10ul};
    terse::DynArray<int, pma::PolyAllocator<int> > arrCopy = std::move(arr);

    ASSERT_TRUE(arr.empty());
    ASSERT_EQ(arr.size(), 0ul);
    ASSERT_EQ(arr.data(), nullptr);

    ASSERT_EQ(arrCopy.size(), 10ul);
    for (std::size_t i{}; i < arrCopy.size(); ++i) {
        ASSERT_EQ(arrCopy[i], values[i]);
    }

    ASSERT_NE(arrCopy.data(), values);
}

TEST(DynArrayTest, MoveAssign) {
    int values[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    terse::DynArray<int, pma::PolyAllocator<int> > arr{values, 10ul};
    terse::DynArray<int, pma::PolyAllocator<int> > arrCopy;
    arrCopy = std::move(arr);

    ASSERT_TRUE(arr.empty());
    ASSERT_EQ(arr.size(), 0ul);
    ASSERT_EQ(arr.data(), nullptr);

    ASSERT_EQ(arrCopy.size(), 10ul);
    for (std::size_t i{}; i < arrCopy.size(); ++i) {
        ASSERT_EQ(arrCopy[i], values[i]);
    }

    ASSERT_NE(arrCopy.data(), values);
}
