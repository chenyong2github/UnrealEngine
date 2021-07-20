// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Library/DMXEntityFixtureType.h"

class UDMXEntityFixturePatch;


class DMXRUNTIME_API FDMXRuntimeUtils
{
public:
	/**
	 * Utility to separate a name from an index at the end.
	 * @param InString	The string to be separated.
	 * @param OutName	The string without an index at the end. White spaces and '_' are also removed.
	 * @param OutIndex	Index that was separated from the name. If there was none, it's zero.
	 *					Check the return value to know if there was an index on InString.
	 * @return True if there was an index on InString.
	 */
	static bool GetNameAndIndexFromString(const FString& InString, FString& OutName, int32& OutIndex);

	/**
	 * Maps each patch to its universe.
	 *
	 * @param AllPatches Patches to map
	 *
	 * @return Key: universe Value: patches in universe with associated key
	 */
	static TMap<int32, TArray<UDMXEntityFixturePatch*>> MapToUniverses(const TArray<UDMXEntityFixturePatch*>& AllPatches);

	/**
	 * Generates a unique name given a base one and a list of potential ones
	 */
	static FString GenerateUniqueNameForImportFunction(TMap<FString, uint32>& OutPotentialFunctionNamesAndCount, const FString& InBaseName);
	
	/**
	 * Sort a distribution array for widget
	 * InDistribution			Distribution schema
	 * InNumXPanels				Num columns
	 * InNumYPanels				Num rows
	 * InUnorderedList			Templated unsorted array input
	 * OutSortedList			Templated unsorted array output
	 */
	template<typename T>
	static void PixelMappingDistributionSort(EDMXPixelMappingDistribution InDistribution, int32 InNumXPanels, int32 InNumYPanels, const TArray<T>& InUnorderedList, TArray<T>& OutSortedList)
	{
		if (InDistribution == EDMXPixelMappingDistribution::TopLeftToRight)
		{	
			// Do nothing it is default, just copy array
			OutSortedList = InUnorderedList;
		}
		else if (InDistribution == EDMXPixelMappingDistribution::TopLeftToBottom)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					OutSortedList.Add(InUnorderedList[XIndex + YIndex * InNumXPanels]);
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::TopLeftToClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((XIndex % 2) == 0)
					{
						OutSortedList.Add(InUnorderedList[YIndex + (XIndex * InNumYPanels)]);
					}
					else
					{
						OutSortedList.Add(InUnorderedList[((XIndex + 1) * InNumYPanels) - (YIndex + 1)]);
					}
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::TopLeftToAntiClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((YIndex % 2) == 0)
					{
						OutSortedList.Add(InUnorderedList[XIndex + YIndex * InNumXPanels]);
					}
					else
					{
						OutSortedList.Add(InUnorderedList[((YIndex + 1) * InNumXPanels) - (XIndex + 1)]);
					}
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::TopRightToLeft)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					OutSortedList.Add(InUnorderedList[(InNumYPanels * (XIndex + 1)) - (YIndex + 1)]);
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomLeftToTop)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					OutSortedList.Add(InUnorderedList[(InNumXPanels - XIndex - 1) + (YIndex * InNumXPanels)]);
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::TopRightToAntiClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((XIndex % 2) == 0)
					{
						OutSortedList.Add(InUnorderedList[(InNumYPanels * (XIndex + 1)) - (YIndex + 1)]);
					}
					else
					{
						OutSortedList.Add(InUnorderedList[YIndex + (XIndex * InNumYPanels)]);
					}
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomLeftToClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((YIndex % 2) == 0)
					{
						OutSortedList.Add(InUnorderedList[(InNumXPanels - XIndex - 1) + (YIndex * InNumXPanels)]);
					}
					else
					{
						OutSortedList.Add(InUnorderedList[XIndex + YIndex * InNumXPanels]);
					}
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomLeftToRight)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					OutSortedList.Add(InUnorderedList[((InNumXPanels - XIndex) * InNumYPanels) - (InNumYPanels - YIndex)]);
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::TopRightToBottom)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					OutSortedList.Add(InUnorderedList[((InNumYPanels - YIndex - 1) * InNumXPanels) + XIndex]);
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomLeftAntiClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((InNumXPanels) % 2 == 0)
					{
						if ((XIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[((InNumXPanels - XIndex) * InNumYPanels) - (YIndex + 1)]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[((InNumXPanels - XIndex) * InNumYPanels) - (InNumYPanels - YIndex)]);
						}
					}
					else
					{
						if ((XIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[((InNumXPanels - XIndex) * InNumYPanels) - (InNumYPanels - YIndex)]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[((InNumXPanels - XIndex) * InNumYPanels) - (YIndex + 1)]);
						}
					}

				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::TopRightToClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((InNumYPanels) % 2 == 0)
					{
						if ((YIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - (YIndex * InNumXPanels) - (1 + XIndex)]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - ((1 + YIndex) * InNumXPanels) + XIndex]);
						}
					}
					else
					{
						if ((YIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - ((1 + YIndex) * InNumXPanels) + XIndex]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - (YIndex * InNumXPanels) - (1 + XIndex)]);
						}
					}
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomRightToLeft)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					OutSortedList.Add(InUnorderedList[((InNumXPanels - XIndex) * InNumYPanels) - (YIndex + 1)]);
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomRightToTop)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - (YIndex * InNumXPanels) - (XIndex + 1)]);
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomRightToClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((InNumXPanels) % 2 == 0)
					{
						if ((XIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - ((XIndex + 1) * InNumYPanels) + YIndex]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[(InNumYPanels * (InNumXPanels - XIndex)) - (YIndex + 1)]);
						}
					}
					else
					{
						if ((XIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[(InNumYPanels * (InNumXPanels - XIndex)) - (YIndex + 1)]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - ((XIndex + 1) * InNumYPanels) + YIndex]);
						}
					}
				}
			}
		}
		else if (InDistribution == EDMXPixelMappingDistribution::BottomRightToAntiClockwise)
		{
			for (int32 XIndex = 0; XIndex < InNumXPanels; ++XIndex)
			{
				for (int32 YIndex = 0; YIndex < InNumYPanels; ++YIndex)
				{
					if ((InNumYPanels) % 2 == 0)
					{
						if ((YIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - ((1 + YIndex) * InNumXPanels) + XIndex]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - (YIndex * InNumXPanels) - (1 + XIndex)]);
						}
					}
					else
					{
						if ((YIndex % 2) == 0)
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - (YIndex * InNumXPanels) - (1 + XIndex)]);
						}
						else
						{
							OutSortedList.Add(InUnorderedList[(InNumXPanels * InNumYPanels) - ((1 + YIndex) * InNumXPanels) + XIndex]);
						}
					}
				}
			}
		}
	}

	// can't instantiate this class
	FDMXRuntimeUtils() = delete;
};
