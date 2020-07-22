// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/IFilter.h"
#include "Misc/FilterCollection.h"
#include "SceneOutlinerFwd.h"

class FMenuBuilder;

namespace SceneOutliner
{
	/**
	 * Contains information used to create a filter which will be displayed as user toggleable filter
	 */
	class FOutlinerFilterInfo
	{ 
	public:
		FOutlinerFilterInfo(const FText& InFilterTitle, const FText& InFilterTooltip, bool bInActive, const FCreateSceneOutlinerFilter& InFactory = FCreateSceneOutlinerFilter())
			: FilterTitle(InFilterTitle)
			, FilterTooltip(InFilterTooltip)
			, bActive(bInActive)
			, Factory(InFactory)
		{}

		/** Initialize and apply a new filter */
		void InitFilter(TSharedPtr<FOutlinerFilters> InFilters);

		/** Add menu for this filter */
		void AddMenu(FMenuBuilder& InMenuBuilder);

		bool IsFilterActive() const;

		DECLARE_EVENT_OneParam(FOutlinerFilterInfo, FOnToggle, bool);
		FOnToggle& OnToggle() { return OnToggleEvent; }

	private:
		void ApplyFilter(bool bActive);
		void ToggleFilterActive();

		TWeakPtr<FOutlinerFilters> Filters;

		TSharedPtr<FOutlinerFilter> Filter;

		FText FilterTitle;
		FText FilterTooltip;
		bool bActive;

		FOnToggle OnToggleEvent;

		FCreateSceneOutlinerFilter Factory;
	};

	/** Enum to specify how items that are not explicitly handled by this filter should be managed */
	enum class EDefaultFilterBehaviour : uint8 { Pass, Fail };

	/** A filter that can be applied to any type in the tree */
	class FOutlinerFilter : public IFilter<const ITreeItem&>
	{
	public:

		/** Event that is fired if this filter changes */
		DECLARE_DERIVED_EVENT(FOutlinerFilter, IFilter<const ITreeItem&>::FChangedEvent, FChangedEvent);
		virtual FChangedEvent& OnChanged() override { return ChangedEvent; }

	protected:

		/**	The event that broadcasts whenever a change occurs to the filter */
		FChangedEvent ChangedEvent;

		/** Default result of the filter when not overridden in derived classes */
		const EDefaultFilterBehaviour DefaultBehaviour;

		/** Constructor to specify the default result of a filter */
		FOutlinerFilter(EDefaultFilterBehaviour InDefaultBehaviour)
			: DefaultBehaviour(InDefaultBehaviour)
		{}

	private:
		/** Transient result from the filter operation. Only valid until the next invocation of the filter. */
		mutable bool bTransientFilterResult;

		/** Check whether the specified item passes our filter */
		virtual bool PassesFilter(const ITreeItem& InItem) const override
		{
			bTransientFilterResult = (DefaultBehaviour == EDefaultFilterBehaviour::Pass);
			return bTransientFilterResult;
		}
	public:
		/** 
		 * Check if an item should be interactive according to this filter.
		 * Default behavior just checks if it passes the filter or not.
		 */
		virtual bool GetInteractiveState(const ITreeItem& InItem) const
		{
			return PassesFilter(InItem);
		}
	};

	/** Outliner filter which will be applied on items which match the specified type */
	template <class T>
	struct TOutlinerFilter : public FOutlinerFilter
	{
		TOutlinerFilter(EDefaultFilterBehaviour InDefaultBehaviour)
			: FOutlinerFilter(InDefaultBehaviour)
		{}

		virtual bool PassesFilter(const ITreeItem& InItem) const override
		{
			if (const T* CastedItem = InItem.CastTo<T>())
			{
				return PassesFilterImpl(*CastedItem);
			}
			return DefaultBehaviour == EDefaultFilterBehaviour::Pass;
		}

		virtual bool PassesFilterImpl(const T& InItem) const
		{
			return DefaultBehaviour == EDefaultFilterBehaviour::Pass;
		}

		virtual bool GetInteractiveState(const ITreeItem& InItem) const
		{
			if (const T* CastedItem = InItem.CastTo<T>())
			{
				return GetInteractiveStateImpl(*CastedItem);
			}
			return DefaultBehaviour == EDefaultFilterBehaviour::Pass;
		}

		// If not overriden will just default to testing against PassesFilter
		virtual bool GetInteractiveStateImpl(const T& InItem) const
		{
			return PassesFilterImpl(InItem);
		}
	};


	/** Predicate based filter for the outliner */
	template <class T>
	struct TOutlinerPredicateFilter : public TOutlinerFilter<T>
	{
		using TFilterPredicate = typename T::FFilterPredicate;
		using TInteractivePredicate = typename T::FInteractivePredicate;

		/** Predicate used to filter tree items */
		mutable TFilterPredicate FilterPred;
		mutable TInteractivePredicate InteractivePred;

		TOutlinerPredicateFilter(TFilterPredicate InFilterPred, EDefaultFilterBehaviour InDefaultBehaviour, TInteractivePredicate InInteractivePredicate = TInteractivePredicate())
			: TOutlinerFilter<T>(InDefaultBehaviour)
			, FilterPred(InFilterPred)
			, InteractivePred(InInteractivePredicate)
		{}

		virtual bool PassesFilterImpl(const T& InItem) const override
		{
			return InItem.Filter(FilterPred);
		}

		virtual bool GetInteractiveStateImpl(const T& InItem) const override 
		{
			if (InteractivePred.IsBound())
			{
				return InItem.GetInteractiveState(InteractivePred);
			}

			// If not interactive state impl is provided, default to interactive if filter passes
			return PassesFilterImpl(InItem);
		}
	};

	/** Scene outliner filters class. This class wraps a collection of filters and allows items of any type to be tested against the entire set. */
	struct FOutlinerFilters : public TFilterCollection<const ITreeItem&>
	{
		/** Overridden to ensure we only ever have FOutlinerFilters added */
		int32 Add(const TSharedPtr<FOutlinerFilter>& Filter)
		{
			return TFilterCollection::Add(Filter);
		}

		/** Test whether this tree item passes all filters, and set its interactive state according to the filter it failed (if applicable) */
		bool GetInteractiveState(const ITreeItem& InItem) const
		{
			for (const auto& Filter : ChildFilters)
			{
				if (!StaticCastSharedPtr<FOutlinerFilter>(Filter)->GetInteractiveState(InItem))
				{
					return false;
				}
			}

			return true;
		}

		/** Add a filter predicate to this filter collection */
		template <typename T>
		void AddFilterPredicate(typename T::FFilterPredicate InFilterPred, EDefaultFilterBehaviour InDefaultBehaviour = EDefaultFilterBehaviour::Pass, typename T::FInteractivePredicate InInteractivePred = T::FInteractivePredicate())
		{
			Add(MakeShareable(new TOutlinerPredicateFilter<T>(InFilterPred, InDefaultBehaviour, InInteractivePred)));
		}
	};
}
