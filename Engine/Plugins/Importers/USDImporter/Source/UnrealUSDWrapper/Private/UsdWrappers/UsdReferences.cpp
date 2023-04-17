// Copyright Epic Games, Inc. All Rights Reserved.

#include "UsdWrappers/UsdReferences.h"

#include "USDMemory.h"

#include "UsdWrappers/UsdPrim.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/references.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

namespace UE
{
	namespace Internal
	{
		class FUsdReferencesImpl
		{
		public:
			FUsdReferencesImpl() = default;

#if USE_USD_SDK
			explicit FUsdReferencesImpl(const pxr::UsdReferences& InUsdReferences)
				: PxrUsdPrim(InUsdReferences.GetPrim())
			{
			}

			explicit FUsdReferencesImpl(pxr::UsdReferences&& InUsdReferences)
				: PxrUsdPrim(InUsdReferences.GetPrim())
			{
			}

			explicit FUsdReferencesImpl(const pxr::UsdPrim& InUsdPrim)
				: PxrUsdPrim(InUsdPrim)
			{
			}

			// pxr::UsdReferences has no default constructor, so we store the prim itself.
			TUsdStore<pxr::UsdPrim> PxrUsdPrim;
#endif // #if USE_USD_SDK
		};
	}

	FUsdReferences::FUsdReferences()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique<Internal::FUsdReferencesImpl>();
	}

	FUsdReferences::FUsdReferences(const FUsdReferences& Other)
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique<Internal::FUsdReferencesImpl>(Other.Impl->PxrUsdPrim.Get());
#endif // #if USE_USD_SDK
	}

	FUsdReferences::FUsdReferences(FUsdReferences&& Other) = default;

	FUsdReferences::FUsdReferences(const FUsdPrim& InPrim)
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique<Internal::FUsdReferencesImpl>(InPrim);
#endif // #if USE_USD_SDK
	}

	FUsdReferences& FUsdReferences::operator=(const FUsdReferences& Other)
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique<Internal::FUsdReferencesImpl>(Other.Impl->PxrUsdPrim.Get());
#endif // #if USE_USD_SDK

		return *this;
	}

	FUsdReferences& FUsdReferences::operator=(FUsdReferences&& Other)
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MoveTemp(Other.Impl);

		return *this;
	}

	FUsdReferences::~FUsdReferences()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl.Reset();
	}

	FUsdReferences::operator bool() const
	{
#if USE_USD_SDK
		return (bool)Impl->PxrUsdPrim.Get();
#else
		return false;
#endif // #if USE_USD_SDK
	}

#if USE_USD_SDK
	FUsdReferences::FUsdReferences(const pxr::UsdReferences& InUsdReferences)
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique<Internal::FUsdReferencesImpl>(InUsdReferences);
	}

	FUsdReferences::FUsdReferences(pxr::UsdReferences&& InUsdReferences)
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique<Internal::FUsdReferencesImpl>(MoveTemp(InUsdReferences));
	}

	FUsdReferences::FUsdReferences(const pxr::UsdPrim& InPrim)
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique<Internal::FUsdReferencesImpl>(InPrim);
	}

#endif // #if USE_USD_SDK

	bool FUsdReferences::AddReference(const FSdfReference& Reference, EUsdListPosition Position)
	{
#if USE_USD_SDK
		static_assert((int32)EUsdListPosition::FrontOfPrependList == (int32)pxr::UsdListPositionFrontOfPrependList, "EUsdListPosition enum doesn't match USD!");
		static_assert((int32)EUsdListPosition::BackOfPrependList == (int32)pxr::UsdListPositionBackOfPrependList, "EUsdListPosition enum doesn't match USD!");
		static_assert((int32)EUsdListPosition::FrontOfAppendList == (int32)pxr::UsdListPositionFrontOfAppendList, "EUsdListPosition enum doesn't match USD!");
		static_assert((int32)EUsdListPosition::BackOfAppendList == (int32)pxr::UsdListPositionBackOfAppendList, "EUsdListPosition enum doesn't match USD!");

		if (Impl->PxrUsdPrim.Get())
		{
			FScopedUsdAllocs UsdAllocs;

			const std::string AssetPath(TCHAR_TO_ANSI(*Reference.AssetPath));
			const pxr::SdfPath UsdPrimPath(Reference.PrimPath);
			const pxr::SdfLayerOffset UsdLayerOffset(Reference.LayerOffset.Offset, Reference.LayerOffset.Scale);

			pxr::SdfReference UsdReference(AssetPath, UsdPrimPath, UsdLayerOffset);

			return Impl->PxrUsdPrim.Get().GetReferences().AddReference(
				UsdReference,
				static_cast<pxr::UsdListPosition>(Position));
		}
		else
		{
			return false;
		}
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdReferences::AddReference(
		const FString& Identifier,
		const FSdfPath& PrimPath,
		const FSdfLayerOffset& LayerOffset,
		EUsdListPosition Position)
	{
#if USE_USD_SDK
		if (Impl->PxrUsdPrim.Get())
		{
			FScopedUsdAllocs UsdAllocs;

			const std::string UsdIdentifier(TCHAR_TO_ANSI(*Identifier));
			const pxr::SdfPath UsdPrimPath(PrimPath);
			const pxr::SdfLayerOffset UsdLayerOffset(LayerOffset.Offset, LayerOffset.Scale);

			return Impl->PxrUsdPrim.Get().GetReferences().AddReference(
				UsdIdentifier, UsdPrimPath, UsdLayerOffset, static_cast<pxr::UsdListPosition>(Position));
		}
		else
		{
			return false;
		}
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdReferences::AddReference(
		const FString& Identifier,
		const FSdfLayerOffset& LayerOffset,
		EUsdListPosition Position)
	{
#if USE_USD_SDK
		if (Impl->PxrUsdPrim.Get())
		{
			FScopedUsdAllocs UsdAllocs;

			const std::string UsdIdentifier(TCHAR_TO_ANSI(*Identifier));
			const pxr::SdfLayerOffset UsdLayerOffset(LayerOffset.Offset, LayerOffset.Scale);

			return Impl->PxrUsdPrim.Get().GetReferences().AddReference(
				UsdIdentifier, UsdLayerOffset, static_cast<pxr::UsdListPosition>(Position));
		}
		else
		{
			return false;
		}
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdReferences::AddInternalReference(
		const FSdfPath& PrimPath,
		const FSdfLayerOffset& LayerOffset,
		EUsdListPosition Position)
	{
#if USE_USD_SDK
		if (Impl->PxrUsdPrim.Get())
		{
			FScopedUsdAllocs UsdAllocs;

			const pxr::SdfPath UsdPrimPath(PrimPath);
			const pxr::SdfLayerOffset UsdLayerOffset(LayerOffset.Offset, LayerOffset.Scale);

			return Impl->PxrUsdPrim.Get().GetReferences().AddInternalReference(
				UsdPrimPath, UsdLayerOffset, static_cast<pxr::UsdListPosition>(Position));
		}
		else
		{
			return false;
		}
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdReferences::RemoveReference(const FSdfReference& Reference)
	{
#if USE_USD_SDK
		if (Impl->PxrUsdPrim.Get())
		{
			FScopedUsdAllocs UsdAllocs;

			const std::string AssetPath(TCHAR_TO_ANSI(*Reference.AssetPath));
			const pxr::SdfPath UsdPrimPath(Reference.PrimPath);
			const pxr::SdfLayerOffset UsdLayerOffset(Reference.LayerOffset.Offset, Reference.LayerOffset.Scale);

			pxr::SdfReference UsdReference(AssetPath, UsdPrimPath, UsdLayerOffset);

			return Impl->PxrUsdPrim.Get().GetReferences().RemoveReference(UsdReference);
		}
		else
		{
			return false;
		}
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdReferences::ClearReferences()
	{
#if USE_USD_SDK
		if (Impl->PxrUsdPrim.Get())
		{
			FScopedUsdAllocs UsdAllocs;

			return Impl->PxrUsdPrim.Get().GetReferences().ClearReferences();
		}
		else
		{
			return false;
		}
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FUsdReferences::SetReferences(const TArray<FSdfReference>& Items)
	{
#if USE_USD_SDK
		if (Impl->PxrUsdPrim.Get())
		{
			FScopedUsdAllocs UsdAllocs;

			std::vector<pxr::SdfReference> UsdReferences;

			for (const FSdfReference& Item : Items)
			{
				const std::string AssetPath(TCHAR_TO_ANSI(*Item.AssetPath));
				const pxr::SdfPath UsdPrimPath(Item.PrimPath);
				const pxr::SdfLayerOffset UsdLayerOffset(Item.LayerOffset.Offset, Item.LayerOffset.Scale);

				UsdReferences.emplace_back(AssetPath, UsdPrimPath, UsdLayerOffset);
			}

			return Impl->PxrUsdPrim.Get().GetReferences().SetReferences(UsdReferences);
		}
		else
		{
			return false;
		}
#else
		return false;
#endif // #if USE_USD_SDK
	}

	FUsdPrim FUsdReferences::GetPrim() const
	{
#if USE_USD_SDK
		if (Impl->PxrUsdPrim.Get())
		{
			return FUsdPrim(Impl->PxrUsdPrim.Get());
		}
		else
		{
			return FUsdPrim{};
		}
#else
		return FUsdPrim{};
#endif // #if USE_USD_SDK
	}
}
