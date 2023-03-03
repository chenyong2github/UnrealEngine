// Copyright Epic Games, Inc. All Rights Reserved.

#include <type_traits>
#include "Templates/IsConst.h"
#include "Templates/UnrealTypeTraits.h"

namespace TypedElementQueryBuilder
{
	namespace Internal
	{
		// This assumes that the types are unique, but for queries this should be true and otherwise
		// both resuls would point to the first found index.
		template<typename Target, typename ArgsCurrent, typename... ArgsRemainder>
		constexpr uint32 GetVarArgIndex()
		{
			if constexpr (TIsSame<Target, ArgsCurrent>::Value)
			{
				return 0;
			}
			else
			{
				return 1 + GetVarArgIndex<Target, ArgsRemainder...>();
			}
		}

		template<typename Type>
		constexpr ITypedElementDataStorageInterface::EQueryAccessType GetAccessType()
		{
			using BaseType = typename TRemoveReference<Type>::Type;
			if constexpr (TIsConst<BaseType>::Value)
			{
				return ITypedElementDataStorageInterface::EQueryAccessType::ReadOnly;
			}
			else
			{
				return ITypedElementDataStorageInterface::EQueryAccessType::ReadWrite;
			}
		}
	}

	//
	// FDependecy
	//

	template<typename... TargetTypes>
	FDependency& FDependency::ReadOnly()
	{
		ReadOnly({ TargetTypes::StaticClass()... });
		return *this;
	}

	template<typename... TargetTypes>
	FDependency& FDependency::ReadWrite()
	{
		ReadWrite({ TargetTypes::StaticClass()... });
		return *this;
	}

	
	//
	// FObserver
	//
	
	template<typename ColumnType>
	FObserver::FObserver(EEvent MonitorForEvent)
		: FObserver(MonitorForEvent, ColumnType::StaticStruct())
	{}

	template<typename ColumnType>
	FObserver& FObserver::SetMonitoredColumn()
	{
		return SetMonitoredColumn(ColumnType::StaticStruct());
	}


	//
	// FCachedQueryContext
	//
	
	template<typename... Subsystems>
	FCachedQueryContext<Subsystems...>::FCachedQueryContext(ITypedElementDataStorageInterface::FQueryContext& InParentContext)
		: FQueryContextForwarder(InParentContext)
		, SubsystemTypes{ Subsystems::StaticClass()... }
		, SubsystemAccessList{ Internal::GetAccessType<Subsystems>()... }
	{
	}

	template<typename... Subsystems>
	void FCachedQueryContext<Subsystems...>::Fetch()
	{
		ParentContext.GetSubsystems(SubsystemAddresses, SubsystemTypes, SubsystemAccessList);
#if DO_CHECK
		char** SubsystemAddessIt = SubsystemAddresses;
		for (int32 Index = 0; Index < sizeof...(Subsystems); ++Index)
		{
			checkf(*SubsystemAddessIt != nullptr, TEXT("The cached query context tried to retrieve a subsystem '%s' that didn't exist."),
				SubsystemTypes[Index] != nullptr ? *(SubsystemTypes[Index]->GetName()) : TEXT("<no type information>"));
			++SubsystemAddessIt;
		}
#endif // DO_CHECK
	}
	
	template<typename... Subsystems>
	void FCachedQueryContext<Subsystems...>::Register(ITypedElementDataStorageInterface::FQueryDescription& Query)
	{
		( Query.Dependencies.Emplace(Subsystems::StaticClass(), Internal::GetAccessType<Subsystems>()), ... );
	}

	template<typename... Subsystems>
	template<typename Subsystem>
	Subsystem* FCachedQueryContext<Subsystems...>::GetCachedMutableSubsystem()
	{
		// Don't allow a subsystem registered as const to be found.
		constexpr uint32 Index = Internal::GetVarArgIndex<std::remove_const_t<Subsystem>, Subsystems...>();
		static_assert(Index < sizeof...(Subsystems), "Requested subsystem isn't part of the query context cache.");
		return reinterpret_cast<Subsystem*>(SubsystemAddresses[Index]);
	}

	template<typename... Subsystems>
	template<typename Subsystem>
	const Subsystem* FCachedQueryContext<Subsystems...>::GetCachedSubsystem() const
	{
		// Allow access to subsystems registered with and without const.
		constexpr uint32 Index = Internal::GetVarArgIndex<Subsystem, Subsystems...>();
		if constexpr (Index < sizeof...(Subsystems))
		{
			return reinterpret_cast<Subsystem*>(SubsystemAddresses[Index]);
		}
		else
		{
			constexpr uint32 ConstIndex = Internal::GetVarArgIndex<std::add_const_t<Subsystem>, Subsystems...>();
			static_assert(ConstIndex < sizeof...(Subsystems), "Requested subsystem isn't part of the query context cache.");
			return reinterpret_cast<Subsystem*>(SubsystemAddresses[ConstIndex]);
		}
	}

	
	//
	// Select
	//
	namespace Internal
	{
		using QueryContext = ITypedElementDataStorageInterface::FQueryContext;
		using QueryAccess = ITypedElementDataStorageInterface::EQueryAccessType;

#		define QUERY_BINDING_BODY(Caller)																					\
			static const UScriptStruct* ColumnTypes[] = { Args::StaticStruct()... };										\
			char* ColumnAddresses[sizeof...(Args)];																			\
			static constexpr QueryAccess ColumnAccess[] = { GetAccessType<Args>()... };										\
			Context.GetColumnsUnguarded(sizeof...(Args), ColumnAddresses, ColumnTypes, ColumnAccess);						\
			uint32 ColumnSize[] = { sizeof(Args)... };																		\
																															\
			using BaseContextType = typename TRemoveReference<ContextType>::Type;											\
			if constexpr(TIsSame<BaseContextType, QueryContext>::Value)														\
			{																												\
				const uint32 RowCount = Context.GetRowCount();																\
				for (uint32 Index = 0; Index < RowCount; ++Index)															\
				{																											\
					Caller (Context, *reinterpret_cast<Args*>(ColumnAddresses[GetVarArgIndex<Args, Args...>()])...);		\
					for (uint32 ColumnIndex = 0; ColumnIndex < sizeof...(Args); ++ColumnIndex)								\
					{																										\
						ColumnAddresses[ColumnIndex] += ColumnSize[ColumnIndex];											\
					}																										\
				}																											\
			}																												\
			else																											\
			{																												\
				BaseContextType CachedContext(Context);																		\
				CachedContext.Fetch();																						\
																															\
				const uint32 RowCount = Context.GetRowCount();																\
				for (uint32 Index = 0; Index < RowCount; ++Index)															\
				{																											\
					Caller(CachedContext, *reinterpret_cast<Args*>(ColumnAddresses[GetVarArgIndex<Args, Args...>()])...);	\
					for (uint32 ColumnIndex = 0; ColumnIndex < sizeof...(Args); ++ColumnIndex)								\
					{																										\
						ColumnAddresses[ColumnIndex] += ColumnSize[ColumnIndex];											\
					}																										\
				}																											\
			}

		template<typename ContextType, typename... Args>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, 
			void (*Callback)(ContextType&, Args&...))
		{
			Function = [Callback](QueryContext& Context)
			{
				QUERY_BINDING_BODY(Callback)
			};
		}

		template<typename Class, typename ContextType, typename... Args>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, Class* Target, 
			void (Class::* Callback)(ContextType&, Args&...))
		{
			Function = [Target, Callback](QueryContext& Context)
			{
				QUERY_BINDING_BODY((Target->*Callback))
			};
		}

		template<typename Class, typename ContextType, typename... Args>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, Class* Target, 
			void (Class::*Callback)(ContextType&, Args&...) const)
		{
			Function = [Target, Callback](QueryContext& Context)
			{
				QUERY_BINDING_BODY((Target->*Callback))
			};
		}

		template<typename Functor, typename Class, typename ContextType, typename... Args>
		void BindQueryFunction_Expand(ITypedElementDataStorageInterface::QueryCallback& Function, Functor CallbackObject, 
			void (Class::* Callback)(ContextType& , Args&...) const)
		{
			Function = [CallbackObject = std::forward<Functor>(CallbackObject)](QueryContext& Context)
			{
				QUERY_BINDING_BODY(CallbackObject)
			};
		}

		template<typename Functor, typename Class, typename ContextType, typename... Args>
		void BindQueryFunction_Expand(ITypedElementDataStorageInterface::QueryCallback& Function, Functor CallbackObject, 
			void (Class::*Callback)(ContextType&, Args&...))
		{
			Function = [CallbackObject = std::forward<Functor>(CallbackObject)](QueryContext& Context)
			{
				QUERY_BINDING_BODY(CallbackObject)
			};
		}

		template<typename Functor>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, Functor Callback)
		{
			BindQueryFunction_Expand(Function, std::forward<Functor>(Callback), &Functor::operator());
		}

#		undef QUERY_BINDING_BODY
		
		template<typename Arg>
		void AddArgumentToSelect(Select& Target)
		{
			using BaseType = typename TRemoveReference<Arg>::Type;
			if constexpr (TIsConst<BaseType>::Value)
			{
				Target.ReadOnly(BaseType::StaticStruct());
			}
			else
			{
				Target.ReadWrite(BaseType::StaticStruct());
			}
		}

		template<typename ContextType> void RegisterDependencies(ITypedElementDataStorageInterface::FQueryDescription& Query)
		{
			using BaseType = typename TRemoveReference<ContextType>::Type;
			if constexpr (TIsDerivedFrom<BaseType, FQueryContextForwarder>::Value)
			{
				BaseType::Register(Query);
			}
		}

		template<typename ContextType, typename... Args>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, void (*)(ContextType&, Args...))
		{ 
			RegisterDependencies<ContextType>(Query);
			(AddArgumentToSelect<Args>(Target), ...); 
		}

		template<typename Class, typename ContextType, typename... Args>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, void (Class::*)(ContextType&, Args...))
		{
			RegisterDependencies<ContextType>(Query);
			(AddArgumentToSelect<Args>(Target), ...); 
		}

		template<typename Class, typename ContextType, typename... Args>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, void (Class::*)(ContextType&, Args...) const)
		{
			RegisterDependencies<ContextType>(Query);
			(AddArgumentToSelect<Args>(Target), ...);
		}
		
		template<typename Functor>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, Functor)
		{ 
			RegisterFunctionArguments(Query, Target, &Functor::operator());
		}

		template <typename T>
		struct HasStaticStructMethod
		{
		private:
			template <typename C> static constexpr bool Check(decltype(&C::StaticStruct)) { return std::is_same_v<decltype(&C::StaticStruct), UScriptStruct*(*)()>; }
			template <typename C> static constexpr bool Check(...) { return false; }

		public:
			static constexpr bool Value = Check<T>(nullptr);
		};

		template<typename Context>
		constexpr bool IsValidContextType()
		{
			using BaseContextType = typename std::remove_const_t<typename TRemoveReference<Context>::Type>;
			if constexpr (TIsSame<BaseContextType, QueryContext>::Value)
			{
				return true;
			}
			else if constexpr (TIsDerivedFrom<BaseContextType, FQueryContextForwarder>::Value)
			{
				return true;
			}
			else
			{
				return false;
			}
		}

		template<typename Arg>
		constexpr bool IsValidArgument()
		{
			if constexpr (TIsReferenceType<Arg>::Value)
			{
				using BaseType = typename std::remove_const_t<typename TRemoveReference<Arg>::Type>;
				return HasStaticStructMethod<BaseType>::Value;
			}
			else
			{
				return false;
			}
		}

		template <typename T>
		class IsFunctor
		{
			template <typename C> static constexpr bool Check(decltype(&C::operator())) { return true; }
			template <typename C> static constexpr bool Check(...) { return false; }

		public:
			static constexpr bool Value = Check<T>(nullptr);
		};

		template<typename T> struct IsValidSelectFunctionSignatureImpl
		{ 
			constexpr static bool Value = false; 
		};

		template<typename ContextType, typename... Args> struct IsValidSelectFunctionSignatureImpl<void (*)(ContextType&, Args...)>
		{ 
			constexpr static bool Value = IsValidContextType<ContextType>() && (IsValidArgument<Args>() && ...);
		};
		
		template<typename Class, typename ContextType, typename... Args> struct IsValidSelectFunctionSignatureImpl<void (Class::*)(ContextType&, Args...)>
		{ 
			constexpr static bool Value = IsValidContextType<ContextType>() && (IsValidArgument<Args>() && ...);
		};

		template<typename Class, typename ContextType, typename... Args> struct IsValidSelectFunctionSignatureImpl<void (Class::*)(ContextType&, Args...) const>
		{ 
			constexpr static bool Value = IsValidContextType<ContextType>() && (IsValidArgument<Args>() && ...);
		};

		template<typename T> constexpr bool IsValidSelectFunctionSignature()
		{ 
			if constexpr (IsFunctor<T>::Value)
			{
				return IsValidSelectFunctionSignatureImpl<decltype(&T::operator())>::Value;
			}
			else
			{
				return IsValidSelectFunctionSignatureImpl<T>::Value;
			}
		};
		
		inline void PrepareForQueryBinding(ITypedElementDataStorageInterface::FQueryDescription& Query, const FProcessor& Processor)
		{
			Query.Callback.Type = ITypedElementDataStorageInterface::EQueryCallbackType::Processor;
			Query.Callback.Phase = Processor.Phase;
			Query.Callback.Group = Processor.Group;
			Query.Callback.BeforeGroup = Processor.BeforeGroup;
			Query.Callback.AfterGroup = Processor.AfterGroup;
			Query.Callback.bForceToGameThread = Processor.bForceToGameThread;
		}

		inline void PrepareForQueryBinding(ITypedElementDataStorageInterface::FQueryDescription& Query, const FObserver& Observer)
		{
			switch (Observer.Event)
			{
			case FObserver::EEvent::Add:
				Query.Callback.Type = ITypedElementDataStorageInterface::EQueryCallbackType::ObserveAdd;
				break;
			case FObserver::EEvent::Remove:
				Query.Callback.Type = ITypedElementDataStorageInterface::EQueryCallbackType::ObserveRemove;
				break;
			}
			Query.Callback.MonitoredType = Observer.Monitor;
			Query.Callback.bForceToGameThread = Observer.bForceToGameThread;
		}
		
		template<typename CallbackType, typename Function>
		void PrepareForQueryBinding(Select& Target, ITypedElementDataStorageInterface::FQueryDescription& Query, FName Name, 
			const CallbackType& Type, Function Callback)
		{
			static_assert(TIsDerivedFrom<CallbackType, FQueryCallbackType>::Value, "The callback type provided isn't one of the available "
				"classes derived from FQueryCallbackType.");
			static_assert(IsValidSelectFunctionSignature<Function>(),
				R"(The function provided to the Query Builder's Select call wasn't invocable or doesn't contain a supported combination of arguments.
The following options are supported:
- void(ITypedElementDataStorageInterface::FQueryContext&, [const]Column&...) 
        e.g. void(ITypedElementDataStorageInterface::FQueryContext& Context, ColumnType0& ColumnA, const ColumnType1& ColumnB)
- void(FCachedQueryContext<[const]Dependencies...>&, [const]Column&...)
		e.g. void(FCachedQueryContext<Subsystem1, const Subsystem2>& Context, ColumnType0& ColumnA, const ColumnType1& ColumnB)
)");
			RegisterFunctionArguments(Query, Target, Callback);
			PrepareForQueryBinding(Query, Type);
			Query.Callback.Name = Name;
		}
	}

	template<typename CallbackType, typename Function>
	Select::Select(FName Name, const CallbackType& Type, Function Callback)
		: Select()
	{
		Internal::PrepareForQueryBinding(*this, Query, Name, Type, Callback);
		Internal::BindQueryFunction(Query.Callback.Function, std::forward<Function>(Callback));
	}

	template<typename CallbackType, typename Class, typename Function>
	Select::Select(FName Name, const CallbackType& Type, Class* Instance, Function Callback)
		: Select()
	{
		Internal::PrepareForQueryBinding(*this, Query, Name, Type, Callback);
		Internal::BindQueryFunction(Query.Callback.Function, Instance, Callback);
	}

	template<typename... TargetTypes>
	Select& Select::ReadOnly()
	{
		ReadOnly({ TargetTypes::StaticStruct()... });
		return *this;
	}

	template<typename... TargetTypes>
	Select& Select::ReadWrite()
	{
		ReadWrite({ TargetTypes::StaticStruct()... });
		return *this;
	}


	//
	// FSimpleQuery
	//

	template<typename... TargetTypes>
	FSimpleQuery& FSimpleQuery::All()
	{
		All({ TargetTypes::StaticStruct()... });
		return *this;
	}

	template<typename... TargetTypes>
	FSimpleQuery& FSimpleQuery::Any()
	{
		Any({ TargetTypes::StaticStruct()... });
		return *this;
	}

	template<typename... TargetTypes>
	FSimpleQuery& FSimpleQuery::None()
	{
		None({ TargetTypes::StaticStruct()... });
		return *this;
	}
} // namespace TypedElementQueryBuilder