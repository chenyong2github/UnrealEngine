// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public static class UhtFunctionSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintAuthorityOnlySpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionFlags |= EFunctionFlags.BlueprintAuthorityOnly;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintCallableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionFlags |= EFunctionFlags.BlueprintCallable;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintCosmeticSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionFlags |= EFunctionFlags.BlueprintCosmetic;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintGetterSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunctionParser Function = (UhtFunctionParser)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
			{
				SpecifierContext.MessageSite.LogError("Function cannot be a blueprint event and a blueprint getter.");
			}

			Function.bSawPropertyAccessor = true;
			Function.FunctionFlags |= EFunctionFlags.BlueprintCallable;
			Function.FunctionFlags |= EFunctionFlags.BlueprintPure;
			Function.MetaData.Add(UhtNames.BlueprintGetter, "");
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintImplementableEventSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunctionParser Function = (UhtFunctionParser)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				SpecifierContext.MessageSite.LogError("BlueprintImplementableEvent functions cannot be replicated!");
			}
			else if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent) && Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
			{
				// already a BlueprintNativeEvent
				SpecifierContext.MessageSite.LogError("A function cannot be both BlueprintNativeEvent and BlueprintImplementableEvent!");
			}
			else if (Function.bSawPropertyAccessor)
			{
				SpecifierContext.MessageSite.LogError("A function cannot be both BlueprintImplementableEvent and a Blueprint Property accessor!");
			}
			else if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Private))
			{
				SpecifierContext.MessageSite.LogError("A Private function cannot be a BlueprintImplementableEvent!");
			}

			Function.FunctionFlags |= EFunctionFlags.Event;
			Function.FunctionFlags |= EFunctionFlags.BlueprintEvent;
			Function.FunctionFlags &= ~EFunctionFlags.Native;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintNativeEventSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunctionParser Function = (UhtFunctionParser)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				SpecifierContext.MessageSite.LogError("BlueprintNativeEvent functions cannot be replicated!");
			}
			else if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent) && !Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
			{
				// already a BlueprintImplementableEvent
				SpecifierContext.MessageSite.LogError("A function cannot be both BlueprintNativeEvent and BlueprintImplementableEvent!");
			}
			else if (Function.bSawPropertyAccessor)
			{
				SpecifierContext.MessageSite.LogError("A function cannot be both BlueprintNativeEvent and a Blueprint Property accessor!");
			}
			else if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Private))
			{
				SpecifierContext.MessageSite.LogError("A Private function cannot be a BlueprintNativeEvent!");
			}

			Function.FunctionFlags |= EFunctionFlags.Event;
			Function.FunctionFlags |= EFunctionFlags.BlueprintEvent;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void BlueprintPureSpecifier(UhtSpecifierContext SpecifierContext, StringView? Value)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;

			// This function can be called, and is also pure.
			Function.FunctionFlags |= EFunctionFlags.BlueprintCallable;

			if (Value == null || UhtFCString.ToBool((StringView)Value))
			{
				Function.FunctionFlags |= EFunctionFlags.BlueprintPure;
			}
			else
			{
				Function.FunctionExportFlags |= UhtFunctionExportFlags.ForceBlueprintImpure;
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void BlueprintSetterSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunctionParser Function = (UhtFunctionParser)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Event))
			{
				SpecifierContext.MessageSite.LogError("Function cannot be a blueprint event and a blueprint setter.");
			}

			Function.bSawPropertyAccessor = true;
			Function.FunctionFlags |= EFunctionFlags.BlueprintCallable;
			Function.MetaData.Add(UhtNames.BlueprintSetter, "");
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void ClientSpecifier(UhtSpecifierContext SpecifierContext, StringView? Value)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				SpecifierContext.MessageSite.LogError("BlueprintImplementableEvent or BlueprintNativeEvent functions cannot be declared as Client or Server");
			}

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Exec))
			{
				SpecifierContext.MessageSite.LogError("Exec functions cannot be replicated!");
			}

			Function.FunctionFlags |= EFunctionFlags.Net;
			Function.FunctionFlags |= EFunctionFlags.NetClient;

			if (Value != null)
			{
				Function.CppImplName = ((StringView)Value).ToString();
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void CustomThunkSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionExportFlags |= UhtFunctionExportFlags.CustomThunk;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ExecSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionFlags |= EFunctionFlags.Exec;
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				SpecifierContext.MessageSite.LogError("Exec functions cannot be replicated!");
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void NetMulticastSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				SpecifierContext.MessageSite.LogError("BlueprintImplementableEvent or BlueprintNativeEvent functions cannot be declared as Multicast");
			}

			Function.FunctionFlags |= EFunctionFlags.Net;
			Function.FunctionFlags |= EFunctionFlags.NetMulticast;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void ReliableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionFlags |= EFunctionFlags.NetReliable;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void SealedEventSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionExportFlags |= UhtFunctionExportFlags.SealedEvent;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void ServerSpecifier(UhtSpecifierContext SpecifierContext, StringView? Value)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				SpecifierContext.MessageSite.LogError("BlueprintImplementableEvent or BlueprintNativeEvent functions cannot be declared as Client or Server");
			}

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Exec))
			{
				SpecifierContext.MessageSite.LogError("Exec functions cannot be replicated!");
			}

			Function.FunctionFlags |= EFunctionFlags.Net;
			Function.FunctionFlags |= EFunctionFlags.NetServer;

			if (Value != null)
			{
				Function.CppImplName = ((StringView)Value).ToString();
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void ServiceRequestSpecifier(UhtSpecifierContext SpecifierContext, List<KeyValuePair<StringView, StringView>> Value)
		{
			UhtFunctionParser Function = (UhtFunctionParser)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				SpecifierContext.MessageSite.LogError("BlueprintImplementableEvent or BlueprintNativeEvent functions cannot be declared as a ServiceRequest");
			}

			Function.FunctionFlags |= EFunctionFlags.Net;
			Function.FunctionFlags |= EFunctionFlags.NetReliable;
			Function.FunctionFlags |= EFunctionFlags.NetRequest;
			Function.FunctionExportFlags |= UhtFunctionExportFlags.CustomThunk;

			ParseNetServiceIdentifiers(SpecifierContext, Value);

			if (string.IsNullOrEmpty(Function.EndpointName))
			{
				SpecifierContext.MessageSite.LogError("ServiceRequest needs to specify an endpoint name");
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void ServiceResponseSpecifier(UhtSpecifierContext SpecifierContext, List<KeyValuePair<StringView, StringView>> Value)
		{
			UhtFunctionParser Function = (UhtFunctionParser)SpecifierContext.Scope.ScopeType;

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				SpecifierContext.MessageSite.LogError("BlueprintImplementableEvent or BlueprintNativeEvent functions cannot be declared as a ServiceResponse");
			}

			Function.FunctionFlags |= EFunctionFlags.Net;
			Function.FunctionFlags |= EFunctionFlags.NetReliable;
			Function.FunctionFlags |= EFunctionFlags.NetResponse;
			//Function.FunctionExportFlags |= EFunctionExportFlags.CustomThunk;

			ParseNetServiceIdentifiers(SpecifierContext, Value);

			if (string.IsNullOrEmpty(Function.EndpointName))
			{
				SpecifierContext.MessageSite.LogError("ServiceResponse needs to specify an endpoint name");
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.Legacy)]
		private static void UnreliableSpecifier(UhtSpecifierContext SpecifierContext)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionExportFlags |= UhtFunctionExportFlags.Unreliable;
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.StringList)]
		private static void WithValidationSpecifier(UhtSpecifierContext SpecifierContext, List<StringView>? Value)
		{
			UhtFunction Function = (UhtFunction)SpecifierContext.Scope.ScopeType;
			Function.FunctionFlags |= EFunctionFlags.NetValidate;

			if (Value != null && Value.Count > 0)
			{
				Function.CppValidationImplName = Value[0].ToString();
			}
		}

		[UhtSpecifierValidator(Extends = UhtTableNames.Function)]
		private static void BlueprintProtectedSpecifierValidator(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value)
		{
			UhtFunction Function = (UhtFunction)Type;
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Static))
			{
				// Given the owning class, locate the class that the owning derives from up to the point of UObject
				if (Function.Outer is UhtClass OuterClass)
				{
					UhtClass? ClassPriorToUObject = OuterClass;
					for (; ClassPriorToUObject != null; ClassPriorToUObject = ClassPriorToUObject.SuperClass)
					{
						// If our super is UObject, then stop
						if (ClassPriorToUObject.Super == Type.Session.UObject)
						{
							break;
						}
					}

					if (ClassPriorToUObject != null && ClassPriorToUObject.SourceName == "UBlueprintFunctionLibrary")
					{
						Type.LogError(MetaData.LineNumber, $"'{Key.Name}' doesn't make sense on static method '{Type.SourceName}' in a blueprint function library");
					}
				}
			}
		}

		[UhtSpecifierValidator(Extends = UhtTableNames.Function)]
		private static void CommutativeAssociativeBinaryOperatorSpecifierValidator(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value)
		{
			UhtFunction Function = (UhtFunction)Type;
			UhtProperty? ReturnProperty = Function.ReturnProperty;
			ReadOnlyMemory<UhtType> ParameterProperties = Function.ParameterProperties;

			if (ReturnProperty == null || ParameterProperties.Length != 2 || !((UhtProperty)ParameterProperties.Span[0]).IsSameType((UhtProperty)ParameterProperties.Span[1]))
			{
				Function.LogError("Commutative associative binary operators must have exactly 2 parameters of the same type and a return value.");
			}
		}

		[UhtSpecifierValidator(Name = "ExpandBoolAsExecs", Extends = UhtTableNames.Function)]
		[UhtSpecifierValidator(Name = "ExpandEnumAsExecs", Extends = UhtTableNames.Function)]
		private static void ExpandsSpecifierValidator(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value)
		{
			// multiple entry parsing in the same format as eg SetParam.
			UhtType? FirstInput = null;
			foreach (string RawGroup in Value.ToString().Split(','))
			{
				foreach (string Entry in RawGroup.Split('|'))
				{
					string Trimmed = Entry.Trim();
					if (string.IsNullOrEmpty(Trimmed))
					{
						continue;
					}

					UhtType? FoundField = Type.FindType(UhtFindOptions.SourceName | UhtFindOptions.SelfOnly | UhtFindOptions.Property, Trimmed, Type);
					if (FoundField != null)
					{
						UhtProperty Property = (UhtProperty)FoundField;
						if (!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm) &&
							(!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm) ||
							 Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReferenceParm)))
						{
							if (FirstInput == null)
							{
								FirstInput = FoundField;
							}
							else
							{
								Type.LogError($"Function already specified an ExpandEnumAsExec input '{FirstInput.SourceName}', but '{Trimmed}' is also an input parameter. Only one is permitted.");
							}
						}
					}
				}
			}
		}

		private static void ParseNetServiceIdentifiers(UhtSpecifierContext SpecifierContext, List<KeyValuePair<StringView, StringView>> Identifiers)
		{
			IUhtTokenReader TokenReader = SpecifierContext.Scope.TokenReader;
			UhtFunctionParser Function = (UhtFunctionParser)SpecifierContext.Scope.ScopeType;

			foreach (var KVP in Identifiers)
			{
				if (KVP.Value.Span.Length > 0)
				{
					if (KVP.Key.Span.StartsWith("Id", StringComparison.OrdinalIgnoreCase))
					{
						int Id;
						if (!int.TryParse(KVP.Value.Span, out Id) || Id <= 0 || Id > UInt16.MaxValue)
						{
							TokenReader.LogError($"Invalid network identifier {KVP.Key} for function");
						}
						else
						{
							Function.RPCId = (UInt16)Id;
						}
					}
					else if (KVP.Key.Span.StartsWith("ResponseId", StringComparison.OrdinalIgnoreCase) || KVP.Key.Span.StartsWith("Priority", StringComparison.OrdinalIgnoreCase))
					{
						int Id;
						if (!int.TryParse(KVP.Value.Span, out Id) || Id <= 0 || Id > UInt16.MaxValue)
						{
							TokenReader.LogError($"Invalid network identifier {KVP.Key} for function");
						}
						else
						{
							Function.RPCResponseId = (UInt16)Id;
						}
					}
					else
					{
						// No error message???
					}
				}

				// Assume it's an endpoint name
				else
				{
					if (!string.IsNullOrEmpty(Function.EndpointName))
					{
						TokenReader.LogError($"Function should not specify multiple endpoints - '{KVP.Key}' found but already using '{Function.EndpointName}'");
					}
					else
					{
						Function.EndpointName = KVP.Key.ToString();
					}
				}
			}
		}
	}
}
