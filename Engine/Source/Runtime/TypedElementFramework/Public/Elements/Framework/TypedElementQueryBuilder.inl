// Copyright Epic Games, Inc. All Rights Reserved.

#include <tuple>
#include <type_traits>
#include <utility>
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
			if constexpr (std::is_same_v<Target, ArgsCurrent>)
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
			using BaseType = typename std::remove_reference_t<Type>;
			if constexpr (TIsConst<BaseType>::Value)
			{
				return ITypedElementDataStorageInterface::EQueryAccessType::ReadOnly;
			}
			else
			{
				return ITypedElementDataStorageInterface::EQueryAccessType::ReadWrite;
			}
		}
	} // namespace Internal

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
	// FQueryContextForwarder
	//

	FQueryContextForwarder::FQueryContextForwarder(ITypedElementDataStorageInterface::FQueryContext& InParentContext)
		: ParentContext(InParentContext)
	{}

	const void* FQueryContextForwarder::GetColumn(const UScriptStruct* ColumnType) const
	{
		return ParentContext.GetColumn(ColumnType);
	}

	void* FQueryContextForwarder::GetMutableColumn(const UScriptStruct* ColumnType)
	{
		return ParentContext.GetMutableColumn(ColumnType);
	}

	void FQueryContextForwarder::GetColumns(TArrayView<char*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes)
	{
		ParentContext.GetColumns(RetrievedAddresses, ColumnTypes, AccessTypes);
	}

	void FQueryContextForwarder::GetColumnsUnguarded(int32 TypeCount, char** RetrievedAddresses, const TWeakObjectPtr<const UScriptStruct>* ColumnTypes,
		const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes)
	{
		ParentContext.GetColumnsUnguarded(TypeCount, RetrievedAddresses, ColumnTypes, AccessTypes);
	}

	UObject* FQueryContextForwarder::GetMutableDependency(const UClass* DependencyClass)
	{
		return ParentContext.GetMutableDependency(DependencyClass);
	}

	const UObject* FQueryContextForwarder::GetDependency(const UClass* DependencyClass)
	{
		return ParentContext.GetDependency(DependencyClass);
	}

	void FQueryContextForwarder::GetDependencies(TArrayView<UObject*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UClass>> DependencyTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes)
	{
		ParentContext.GetDependencies(RetrievedAddresses, DependencyTypes, AccessTypes);
	}

	uint32 FQueryContextForwarder::GetRowCount() const
	{
		return ParentContext.GetRowCount();
	}

	TConstArrayView<TypedElementRowHandle> FQueryContextForwarder::GetRowHandles() const
	{
		return ParentContext.GetRowHandles();
	}

	void FQueryContextForwarder::RemoveRow(TypedElementRowHandle Row)
	{
		ParentContext.RemoveRow(Row);
	}

	void FQueryContextForwarder::RemoveRows(TConstArrayView<TypedElementRowHandle> Rows)
	{
		ParentContext.RemoveRows(Rows);
	}

	void FQueryContextForwarder::AddColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		ParentContext.AddColumns(Row, ColumnTypes);
	}

	void FQueryContextForwarder::AddColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		ParentContext.AddColumns(Rows, ColumnTypes);
	}

	void FQueryContextForwarder::RemoveColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		ParentContext.RemoveColumns(Row, ColumnTypes);
	}

	void FQueryContextForwarder::RemoveColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		ParentContext.RemoveColumns(Rows, ColumnTypes);
	}

	//
	// FCachedQueryContext
	//
	
	template<typename... Dependencies>
	FCachedQueryContext<Dependencies...>::FCachedQueryContext(ITypedElementDataStorageInterface::FQueryContext& InParentContext)
		: FQueryContextForwarder(InParentContext)
	{
		static constexpr ITypedElementDataStorageInterface::EQueryAccessType DependencyAccessList[] = { Internal::GetAccessType<Dependencies>()... };
		static TWeakObjectPtr<const UClass> DependencyTypes[] = { Dependencies::StaticClass()... };

		ParentContext.GetDependencies(DependencyAddresses, DependencyTypes, DependencyAccessList);
#if DO_CHECK
		UObject** DependencyAddessIt = DependencyAddresses;
		for (int32 Index = 0; Index < sizeof...(Dependencies); ++Index)
		{
			checkf(*DependencyAddessIt != nullptr, TEXT("The cached query context tried to retrieve a dependency '%s' that didn't exist."),
				DependencyTypes[Index] != nullptr ? *(DependencyTypes[Index]->GetName()) : TEXT("<no type information>"));
			++DependencyAddessIt;
		}
#endif // DO_CHECK
	}

	template<typename... Dependencies>
	void FCachedQueryContext<Dependencies...>::Register(ITypedElementDataStorageInterface::FQueryDescription& Query)
	{
		( Query.Dependencies.Emplace(Dependencies::StaticClass(), Internal::GetAccessType<Dependencies>()), ... );
	}

	template<typename... Dependencies>
	template<typename Dependency>
	Dependency& FCachedQueryContext<Dependencies...>::GetCachedMutableDependency()
	{
		// Don't allow a subsystem registered as const to be found.
		constexpr uint32 Index = Internal::GetVarArgIndex<std::remove_const_t<Dependency>, Dependencies...>();
		static_assert(Index < sizeof...(Dependencies), "Requested dependency isn't part of the query context cache.");
		return *static_cast<Dependency*>(DependencyAddresses[Index]);
	}

	template<typename... Dependencies>
	template<typename Dependency>
	const Dependency& FCachedQueryContext<Dependencies...>::GetCachedDependency() const
	{
		// Allow access to subsystems registered with and without const.
		constexpr uint32 Index = Internal::GetVarArgIndex<Dependency, Dependencies...>();
		if constexpr (Index < sizeof...(Dependencies))
		{
			return *static_cast<Dependency*>(DependencyAddresses[Index]);
		}
		else
		{
			constexpr uint32 ConstIndex = Internal::GetVarArgIndex<std::add_const_t<Dependency>, Dependencies...>();
			static_assert(ConstIndex < sizeof...(Dependencies), "Requested dependency isn't part of the query context cache.");
			return *static_cast<Dependency*>(DependencyAddresses[ConstIndex]);
		}
	}
	
	//
	// Select
	//
	namespace Internal
	{
		using QueryContext = ITypedElementDataStorageInterface::FQueryContext;
		using QueryAccess = ITypedElementDataStorageInterface::EQueryAccessType;

		template <typename T>
		struct HasStaticStructMethod
		{
		private:
			template <typename C> static constexpr bool Check(decltype(&C::StaticStruct)) { return std::is_same_v<decltype(&C::StaticStruct), UScriptStruct* (*)()>; }
			template <typename C> static constexpr bool Check(...) { return false; }

		public:
			static constexpr bool Value = Check<T>(nullptr);
		};

		template<typename Context>
		constexpr bool IsValidContextType()
		{
			using BaseContextType = typename std::remove_const_t<typename TRemoveReference<Context>::Type>;
			if constexpr (std::is_same_v<BaseContextType, QueryContext>)
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

		template<typename... Columns>
		constexpr bool AreAllColumnsReferences()
		{
			if constexpr (sizeof...(Columns) > 0)
			{
				return (std::is_reference_v<Columns> && ...);
			}
			else
			{
				return true;
			}
		}

		template<typename... Columns>
		constexpr bool AreAllColumnsPointers()
		{
			if constexpr (sizeof...(Columns) > 0)
			{
				return (std::is_pointer_v<Columns> && ...);
			}
			else
			{
				return true;
			}
		}

		template<typename Column>
		using BaseColumnType = std::remove_pointer_t<std::remove_reference_t<Column>>;

		template<typename Column>
		using UndecoratedColumnType = std::remove_cv_t<BaseColumnType<Column>>;

		template<typename Column>
		constexpr bool IsValidColumnType()
		{
			// Do not combine these two into a single statement as that would also allow invalid arguments like "MyColumn*&"
			if constexpr (std::is_reference_v<Column>)
			{
				using BaseType = typename std::remove_const_t<std::remove_reference_t<Column>>;
				return HasStaticStructMethod<BaseType>::Value;
			}
			else if constexpr (std::is_pointer_v<Column>)
			{
				using BaseType = typename std::remove_const_t<std::remove_pointer_t<Column>>;
				return HasStaticStructMethod<BaseType>::Value;
			}
			else
			{
				return false;
			}
		}

		template<typename Column>
		const UScriptStruct* GetColumnType()
		{
			return UndecoratedColumnType<Column>::StaticStruct();
		}

		template<typename RowType>
		constexpr bool IsRowHandleType()
		{
			return std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RowType>>, TypedElementRowHandle>;
		}

		template<typename RowType, typename... Columns>
		constexpr bool IsRowTypeCompatibleWithColumns()
		{
			if constexpr (std::is_pointer_v<RowType> && std::is_const_v<std::remove_pointer_t<RowType>>)
			{
				return AreAllColumnsPointers<Columns...>();
			}
			else
			{
				return AreAllColumnsReferences<Columns...>();
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

		template<typename... ColumnTypes>
		struct FFunctionColumnInfo
		{
			std::tuple<BaseColumnType<ColumnTypes>*...> Columns;
			static constexpr bool bArePointerColumns = AreAllColumnsPointers<ColumnTypes...>();

			explicit FFunctionColumnInfo(QueryContext& Context)
			{
				static constexpr QueryAccess ColumnAccess[] = { GetAccessType<ColumnTypes>()... };
				static TWeakObjectPtr<const UScriptStruct> ColumnDescriptions[] = { GetColumnType<ColumnTypes>()...};

				char* ColumnAddresses[sizeof...(ColumnTypes)];
				Context.GetColumnsUnguarded(sizeof...(ColumnTypes), ColumnAddresses, ColumnDescriptions, ColumnAccess);
				std::apply([ColumnAddresses](auto&&... Column)
					{
						int Index = 0;
						((Column = reinterpret_cast<BaseColumnType<ColumnTypes>*>(ColumnAddresses[Index++])), ...);
					}, Columns);
			}
		};

		template<>
		struct FFunctionColumnInfo<>
		{
			explicit FFunctionColumnInfo(QueryContext&) {}
			static constexpr bool bArePointerColumns = false;
		};

		template<typename ContextType>
		struct FContextInfo
		{
			using BaseContextType = std::remove_reference_t<ContextType>;
			static constexpr bool bIsUndecoratedContext = std::is_same_v<BaseContextType, QueryContext>;
			using ContextWrapperType = std::conditional_t<bIsUndecoratedContext, QueryContext&, BaseContextType>;
			ContextWrapperType ContextWrapper;

			explicit FContextInfo(QueryContext& Context)
				: ContextWrapper(Context)
			{}
		};

		template<typename ContextType, typename RowHandleType, typename... Args>
		struct FContextRowHandleColumnsFunction : public FFunctionColumnInfo<Args...>, public FContextInfo<ContextType>
		{
			using SuperColumn = FFunctionColumnInfo<Args...>;
			using SuperContext = FContextInfo<ContextType>;

			explicit FContextRowHandleColumnsFunction(QueryContext& Context)
				: SuperColumn(Context)
				, SuperContext(Context)
			{}

			template<typename CallerType>
			void Call(QueryContext& Context, CallerType&& Caller)
			{
				TConstArrayView<TypedElementRowHandle> Rows = Context.GetRowHandles();
				if constexpr (SuperColumn::bArePointerColumns || std::is_pointer_v<RowHandleType>)
				{
					if constexpr (sizeof...(Args) > 0)
					{
						std::apply([this, Rows = Rows.GetData(), &Caller](auto&&... Column)
							{
								Caller(this->ContextWrapper, Rows, Column...);
							}, this->Columns);
					}
					else
					{
						Caller(this->ContextWrapper, Rows.GetData());
					}
				}
				else
				{
					if constexpr (sizeof...(Args) > 0)
					{
						for (TypedElementRowHandle Row : Rows)
						{
							if constexpr (sizeof...(Args) > 0)
							{
								std::apply([this, Row, &Caller](auto&&... Column)
									{
										Caller(this->ContextWrapper, Row, *Column...);
										((++Column), ...);
									}, this->Columns);
							}
							else
							{
								Caller(this->ContextWrapper, Row);
							}
						}
					}
				}
			}
		};

		template<typename ContextType, typename... Args>
		struct FContextColumnsFunction : public FFunctionColumnInfo<Args...>, public FContextInfo<ContextType>
		{
			using SuperColumn = FFunctionColumnInfo<Args...>;
			using SuperContext = FContextInfo<ContextType>;

			explicit FContextColumnsFunction(QueryContext& Context)
				: SuperColumn(Context)
				, SuperContext(Context)
			{}

			template<typename CallerType>
			void Call(QueryContext& Context, CallerType&& Caller)
			{
				if constexpr (sizeof...(Args) > 0)
				{
					if constexpr (SuperColumn::bArePointerColumns)
					{
						std::apply(
							[this, Caller = std::forward<CallerType>(Caller)](auto&&... Column)
							{
								Caller(this->ContextWrapper, Column...);
							}, this->Columns);
					}
					else
					{
						auto CallForwarder = [this, Caller = std::forward<CallerType>(Caller)](auto&&... Column)
						{
							Caller(this->ContextWrapper, *Column...);
							((++Column), ...);
						};
						const uint32 RowCount = Context.GetRowCount();
						for (uint32 Index = 0; Index < RowCount; ++Index)
						{
							std::apply(CallForwarder, this->Columns);
						}
					}
				}
				else
				{
					const uint32 RowCount = Context.GetRowCount();
					for (uint32 Index = 0; Index < RowCount; ++Index)
					{
						Caller(this->ContextWrapper);
					}
				}
			}
		};

		template<typename RowHandleType, typename... Columns>
		struct FRowHandleColumnsFunction : public FFunctionColumnInfo<Columns...>
		{
			using Super = FFunctionColumnInfo<Columns...>;

			explicit FRowHandleColumnsFunction(QueryContext& Context)
				: Super(Context)
			{}

			template<typename CallerType>
			void Call(QueryContext& Context, CallerType&& Caller)
			{
				TConstArrayView<TypedElementRowHandle> Rows = Context.GetRowHandles();
				if constexpr (Super::bArePointerColumns || std::is_pointer_v<RowHandleType>)
				{
					if constexpr (sizeof...(Columns) > 0)
					{
						std::apply([Rows = Rows.GetData(), &Caller](auto&&... Column)
							{
								Caller(Rows, Column...);
							}, this->Columns);
					}
					else
					{
						Caller(Rows.GetData());
					}
				}
				else
				{
					for (TypedElementRowHandle Row : Rows)
					{
						if constexpr (sizeof...(Columns) > 0)
						{
							std::apply([Row, &Caller](auto&&... Column)
								{
									Caller(Row, *Column...);
									((++Column), ...);
								}, this->Columns);
						}
						else
						{
							Caller(Row);
						}
					}
				}
			}
		};

		template<typename... Args>
		struct FColumnsFunction : public FFunctionColumnInfo<Args...>
		{
			using Super = FFunctionColumnInfo<Args...>;
			
			explicit FColumnsFunction(QueryContext& Context)
				: Super(Context)
			{}

			template<typename CallerType>
			void Call(QueryContext& Context, CallerType&& Caller)
			{
				if constexpr (sizeof...(Args) > 0)
				{
					if constexpr (Super::bArePointerColumns)
					{
						std::apply(
							[Caller = std::forward<CallerType>(Caller)](auto&&... Column)
							{
								Caller(Column...);
							}, this->Columns);
					}
					else
					{
						auto CallForwarder = [Caller = std::forward<CallerType>(Caller)](auto&&... Column)
						{
							Caller(*Column...);
							((++Column), ...);
						};
						const uint32 RowCount = Context.GetRowCount();
						for (uint32 Index = 0; Index < RowCount; ++Index)
						{
							std::apply(CallForwarder, this->Columns);
						}
					}
				}
				else
				{
					const uint32 RowCount = Context.GetRowCount();
					for (uint32 Index = 0; Index < RowCount; ++Index)
					{
						Caller();
					}
				}
			}
		};

		template<typename... Args>
		struct FFunctionInfoHelper
		{
			using BaseClass = FColumnsFunction<Args...>;
		};

		template<typename Arg0>
		struct FFunctionInfoHelper<Arg0>
		{
			static constexpr bool bArg0IsContext = IsValidContextType<Arg0>();
			static constexpr bool bArg0IsRowHandle = IsRowHandleType<Arg0>();
			using BaseClass = 
				std::conditional_t<bArg0IsContext,
					FContextColumnsFunction<Arg0>,
					std::conditional_t<bArg0IsRowHandle,
						FRowHandleColumnsFunction<Arg0>,
						FColumnsFunction<Arg0>
					>
				>;
		};

		template<typename Arg0, typename Arg1, typename... Args>
		struct FFunctionInfoHelper<Arg0, Arg1, Args...>
		{
			static constexpr bool bArg0IsContext = IsValidContextType<Arg0>();
			static constexpr bool bArg0IsRowHandle = IsRowHandleType<Arg0>();
			
			static constexpr bool bArg1IsRowHandle = IsRowHandleType<Arg1>();

			using BaseClass =
				std::conditional_t<bArg0IsContext,
					std::conditional_t<bArg1IsRowHandle,
						FContextRowHandleColumnsFunction<Arg0, Arg1, Args...>,
						FContextColumnsFunction<Arg0, Arg1, Args...>
					>,
					std::conditional_t<bArg0IsRowHandle,
						FRowHandleColumnsFunction<Arg0, Arg1, Args...>,
						FColumnsFunction<Arg0, Arg1, Args...>
					>
				>;
		};

		template<typename... Args>
		struct FFunctionInfo final : public FFunctionInfoHelper<Args...>::BaseClass
		{
			using Super = typename FFunctionInfoHelper<Args...>::BaseClass;

			explicit FFunctionInfo(QueryContext& Context)
				: Super(Context)
			{}
		};

		template<typename... Args>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, 
			void (*Callback)(Args...))
		{
			Function = [Callback](QueryContext& Context)
			{
				FFunctionInfo<Args...>(Context).Call(Context, Callback);
			};
		}

		template<typename Class, typename... Args>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, Class* Target, 
			void (Class::*Callback)(Args...))
		{
			Function = [Target, Callback](QueryContext& Context)
			{
				FFunctionInfo<Args...>(Context).Call(Context, (Target->*Callback));
			};
		}

		template<typename Class, typename... Args>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, Class* Target, 
			void (Class::*Callback)(Args...) const)
		{
			Function = [Target, Callback](QueryContext& Context)
			{
				FFunctionInfo<Args...>(Context).Call(Context, (Target->*Callback));
			};
		}

		template<typename Functor, typename Class, typename... Args>
		void BindQueryFunction_Expand(ITypedElementDataStorageInterface::QueryCallback& Function, Functor&& CallbackObject, 
			void (Class::*Callback)(Args...) const)
		{
			Function = [CallbackObject = std::forward<Functor>(CallbackObject)](QueryContext& Context)
			{
				FFunctionInfo<Args...>(Context).Call(Context, CallbackObject);
			};
		}

		template<typename Functor, typename Class, typename... Args>
		void BindQueryFunction_Expand(ITypedElementDataStorageInterface::QueryCallback& Function, Functor&& CallbackObject, 
			void (Class::*Callback)(Args...))
		{
			Function = [CallbackObject = std::forward<Functor>(CallbackObject)](QueryContext& Context)
			{
				FFunctionInfo<Args...>(Context).Call(Context, CallbackObject);
			};
		}

		template<typename Functor>
		void BindQueryFunction(ITypedElementDataStorageInterface::QueryCallback& Function, Functor&& Callback)
		{
			BindQueryFunction_Expand(Function, std::forward<Functor>(Callback), &Functor::operator());
		}
		
		template<typename Arg>
		void AddColumnToSelect(Select& Target)
		{
			if constexpr (std::is_const_v<BaseColumnType<Arg>>)
			{
				Target.ReadOnly(UndecoratedColumnType<Arg>::StaticStruct());
			}
			else
			{
				Target.ReadWrite(UndecoratedColumnType<Arg>::StaticStruct());
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

		template<typename... Args>
		struct RegisterFunctionArgumentsHelper
		{
			void Register(ITypedElementDataStorageInterface::FQueryDescription&, Select& Target)
			{
				(AddColumnToSelect<Args>(Target), ...);
			}
		};

		template<>
		struct RegisterFunctionArgumentsHelper<>
		{
			static void Register(ITypedElementDataStorageInterface::FQueryDescription&, Select&) {}
		};

		template<typename Arg0>
		struct RegisterFunctionArgumentsHelper<Arg0>
		{
			static void Register(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target)
			{
				if constexpr (IsValidContextType<Arg0>())
				{
					RegisterDependencies<Arg0>(Query);
				}
				else if constexpr (IsValidColumnType<Arg0>())
				{
					AddColumnToSelect<Arg0>(Target);
				}
				// The third option is a row handle, which doesn't need to be registered.
			}
		};

		template<typename Arg0, typename Arg1, typename... Args>
		struct RegisterFunctionArgumentsHelper<Arg0, Arg1, Args...>
		{
			static void Register(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target)
			{
				if constexpr (IsValidContextType<Arg0>())
				{
					RegisterDependencies<Arg0>(Query);
				}
				else if constexpr (IsValidColumnType<Arg0>())
				{
					AddColumnToSelect<Arg0>(Target);
				}
				// The third option is a row handle, which doesn't need to be registered.

				if constexpr (IsValidColumnType<Arg1>())
				{
					AddColumnToSelect<Arg1>(Target);
				}
				// The second option is a row handle, which doesn't need to be registered.

				(AddColumnToSelect<Args>(Target), ...);
			}
		};

		template<typename... Args>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, void (*)(Args...))
		{
			RegisterFunctionArgumentsHelper<Args...>::Register(Query, Target);
		}

		template<typename Class, typename... Args>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, void (Class::*)(Args...))
		{
			RegisterFunctionArgumentsHelper<Args...>::Register(Query, Target);
		}

		template<typename Class, typename... Args>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, void (Class::*)(Args...) const)
		{
			RegisterFunctionArgumentsHelper<Args...>::Register(Query, Target);
		}

		template<typename Functor>
		void RegisterFunctionArguments(ITypedElementDataStorageInterface::FQueryDescription& Query, Select& Target, Functor)
		{
			RegisterFunctionArguments(Query, Target, &Functor::operator());
		}

		template<typename... Args>
		struct IsValidSelectFunctionSignatureImpl2
		{
			constexpr static bool Value = (IsValidColumnType<Args>() && ...);
		};

		template<typename Arg0, typename... Args>
		struct IsValidSelectFunctionSignatureImpl2<Arg0, Args...>
		{
			constexpr static bool Arg0IsContext = IsValidContextType<Arg0>();
			constexpr static bool Arg0IsRowHandle = IsRowHandleType<Arg0>();
			constexpr static bool Arg0IsArgument = IsValidColumnType<Arg0>();
			constexpr static bool Arg0IsValidRowHandle = Arg0IsRowHandle && IsRowTypeCompatibleWithColumns<Arg0, Args...>();
			constexpr static bool Arg0IsValid = (Arg0IsContext || Arg0IsValidRowHandle || Arg0IsArgument);
			
			static_assert(!Arg0IsRowHandle || Arg0IsValidRowHandle, "Row handles need to taken by value when the columns are requested by "
				"reference or by const pointer if the columns are taken by reference.");
			static_assert(Arg0IsValid, "The first provided argument has to be a reference to a compatible query reference, a row handle"
				" or a (const) reference/pointer to a column.");
			
			constexpr static bool Value = Arg0IsValid && (IsValidColumnType<Args>() && ...);
		};

		template<typename Arg0, typename Arg1, typename... Args>
		struct IsValidSelectFunctionSignatureImpl2<Arg0, Arg1, Args...>
		{
			constexpr static bool Arg0IsContext = IsValidContextType<Arg0>();
			constexpr static bool Arg0IsRowHandle = IsRowHandleType<Arg0>();
			constexpr static bool Arg0IsArgument = IsValidColumnType<Arg0>();
			constexpr static bool Arg0IsValidRowHandle = Arg0IsRowHandle && IsRowTypeCompatibleWithColumns<Arg0, Args...>();
			constexpr static bool Arg0IsValid = (Arg0IsContext || Arg0IsValidRowHandle || Arg0IsArgument);
			
			static_assert(!Arg0IsRowHandle || Arg0IsValidRowHandle, "Row handles need to taken by value when the columns are requested by "
				"reference or by const pointer if the columns are taken by reference.");
			static_assert(Arg0IsValid, "The first argument to a query callback has to be a reference to a compatible query reference, a "
				"row handle or a (const) reference/pointer to a column.");
			
			constexpr static bool Arg1IsRowHandle = IsRowHandleType<Arg1>();
			constexpr static bool Arg1IsArgument = IsValidColumnType<Arg1>();
			constexpr static bool Arg1IsValidRowHandle = Arg1IsRowHandle && IsRowTypeCompatibleWithColumns<Arg1, Args...>();
			constexpr static bool Arg1IsValid = (Arg1IsValidRowHandle || Arg1IsArgument);

			static_assert(!Arg1IsRowHandle || Arg1IsValidRowHandle, "Row handles need to taken by value when the columns are requested by "
				"reference or by const pointer if the columns are taken by reference.");
			static_assert(Arg1IsValid, "The second argument to a query callback has to be a row handle or a (const) reference/pointer "
				"to a column.");
			
			constexpr static bool Value = Arg0IsValid && Arg1IsValid && (IsValidColumnType<Args>() && ...);
		};

		template<>
		struct IsValidSelectFunctionSignatureImpl2<>
		{
			constexpr static bool Value = false;
		};
				
		template<typename T> struct IsValidSelectFunctionSignatureImpl
		{ 
			constexpr static bool Value = false; 
		};

		template<typename... Args> struct IsValidSelectFunctionSignatureImpl<void (*)(Args...)>
		{ 
			constexpr static bool Value = IsValidSelectFunctionSignatureImpl2<Args...>::Value;
		};
		
		template<typename Class, typename... Args> struct IsValidSelectFunctionSignatureImpl<void (Class::*)(Args...)>
		{ 
			constexpr static bool Value = IsValidSelectFunctionSignatureImpl2<Args...>::Value;
		};

		template<typename Class, typename... Args> struct IsValidSelectFunctionSignatureImpl<void (Class::*)(Args...) const>
		{ 
			constexpr static bool Value = IsValidSelectFunctionSignatureImpl2<Args...>::Value;
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
- void([const]Column&...) 
- void(TypedElementRowHandle, [const]Column&...) 
- void(<Context>&, [const]Column&...) 
- void(<Context>&, TypedElementRowHandle, [const]Column&...) 
- void(<Context>&, [const]Column*...) 
- void(<Context>&, const TypedElementRowHandle*, [const]Column*...) 
Where <Context> is ITypedElementDataStorageInterface::FQueryContext or FCachedQueryContext<...>
e.g. void(FCachedQueryContext<Subsystem1, const Subsystem2>& Context, TypedElementRowHandle Row, ColumnType0& ColumnA, const ColumnType1& ColumnB) {...}
)");
			RegisterFunctionArguments(Query, Target, Callback);
			PrepareForQueryBinding(Query, Type);
			Query.Callback.Name = Name;
		}
	}

	template<typename CallbackType, typename Function>
	Select::Select(FName Name, const CallbackType& Type, Function&& Callback)
		: Select()
	{
		Internal::PrepareForQueryBinding(*this, Query, Name, Type, Callback);
		Internal::BindQueryFunction(Query.Callback.Function, std::forward<Function>(Callback));
	}

	template<typename CallbackType, typename Class, typename Function>
	Select::Select(FName Name, const CallbackType& Type, Class* Instance, Function&& Callback)
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