// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal class UhtHeaderCodeGenerator : UhtPackageCodeGenerator
	{
		#region Define block macro names
		public const string EventParamsMacroSuffix = "EVENT_PARMS";
		public const string CallbackWrappersMacroSuffix = "CALLBACK_WRAPPERS";
		public const string SparseDataMacroSuffix = "SPARSE_DATA";
		public const string EditorOnlyRpcWrappersMacroSuffix = "EDITOR_ONLY_RPC_WRAPPERS";
		public const string RpcWrappersMacroSuffix = "RPC_WRAPPERS";
		public const string EditorOnlyRpcWrappersNoPureDeclsMacroSuffix = "EDITOR_ONLY_RPC_WRAPPERS_NO_PURE_DECLS";
		public const string RpcWrappersNoPureDeclsMacroSuffix = "RPC_WRAPPERS_NO_PURE_DECLS";
		public const string AccessorsMacroSuffix = "ACCESSORS";
		public const string ArchiveSerializerMacroSuffix = "ARCHIVESERIALIZER";
		public const string StandardConstructorsMacroSuffix = "STANDARD_CONSTRUCTORS";
		public const string EnchancedConstructorsMacroSuffix = "ENHANCED_CONSTRUCTORS";
		public const string GeneratedBodyMacroSuffix = "GENERATED_BODY";
		public const string GeneratedBodyLegacyMacroSuffix = "GENERATED_BODY_LEGACY";
		public const string GeneratedUInterfaceBodyMacroSuffix = "GENERATED_UINTERFACE_BODY()";
		public const string InClassMacroSuffix = "INCLASS";
		public const string InClassNoPureDeclsMacroSuffix = "INCLASS_NO_PURE_DECLS";
		public const string InClassIInterfaceMacroSuffix = "INCLASS_IINTERFACE";
		public const string InClassIInterfaceNoPureDeclsMacroSuffix = "INCLASS_IINTERFACE_NO_PURE_DECLS";
		public const string PrologMacroSuffix = "PROLOG";
		public const string DelegateMacroSuffix = "DELEGATE";
		#endregion

		public readonly UhtHeaderFile HeaderFile;
		public string FileId => this.HeaderInfos[this.HeaderFile.HeaderFileTypeIndex].FileId;

		public UhtHeaderCodeGenerator(UhtCodeGenerator CodeGenerator, UhtPackage Package, UhtHeaderFile HeaderFile)
			: base(CodeGenerator, Package)
		{
			this.HeaderFile = HeaderFile;
		}

		#region Event parameter
		protected StringBuilder AppendEventParameter(StringBuilder Builder, UhtFunction Function, string StrippedFunctionName, UhtPropertyTextType TextType, bool bOutputConstructor, int Tabs, string Endl)
		{
			if (!WillExportEventParameters(Function))
			{
				return Builder;
			}

			string EventParameterStructName = GetEventStructParametersName(Function.Outer, StrippedFunctionName);

			Builder.AppendTabs(Tabs).Append("struct ").Append(EventParameterStructName).Append(Endl);
			Builder.AppendTabs(Tabs).Append("{").Append(Endl);

			++Tabs;
			foreach (UhtProperty Property in Function.Children)
			{
				bool bEmitConst = Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm) && Property is UhtObjectProperty;

				//@TODO: UCREMOVAL: This is awful code duplication to avoid a double-const
				{
					//@TODO: bEmitConst will only be true if we have an object, so checking interface here doesn't do anything.
					// export 'const' for parameters
					bool bIsConstParam = Property is UhtInterfaceProperty && !Property.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm); //@TODO: This should be const once that flag exists
					bool bIsOnConstClass = false;
					if (Property is UhtObjectProperty ObjectProperty)
					{
						bIsOnConstClass = ObjectProperty.Class.ClassFlags.HasAnyFlags(EClassFlags.Const);
					}

					if (bIsConstParam || bIsOnConstClass)
					{
						bEmitConst = false; // ExportCppDeclaration will do it for us
					}
				}

				Builder.AppendTabs(Tabs);
				if (bEmitConst)
				{
					Builder.Append("const ");
				}

				Builder.AppendFullDecl(Property, TextType, false);
				Builder.Append(";").Append(Endl);
			}

			if (bOutputConstructor)
			{
				UhtProperty? ReturnProperty = Function.ReturnProperty;
				if (ReturnProperty != null && ReturnProperty.PropertyCaps.HasAnyFlags(UhtPropertyCaps.RequiresNullConstructorArg))
				{
					Builder.Append(Endl);
					Builder.AppendTabs(Tabs).Append("/** Constructor, initializes return property only **/").Append(Endl);
					Builder.AppendTabs(Tabs).Append(EventParameterStructName).Append("()").Append(Endl);
					Builder.AppendTabs(Tabs + 1).Append(": ").Append(ReturnProperty.SourceName).Append('(').AppendNullConstructorArg(ReturnProperty, true).Append(")").Append(Endl);
					Builder.AppendTabs(Tabs).Append("{").Append(Endl);
					Builder.AppendTabs(Tabs).Append("}").Append(Endl);
				}
			}
			--Tabs;

			Builder.AppendTabs(Tabs).Append("};").Append(Endl);

			return Builder;
		}

		protected static bool WillExportEventParameters(UhtFunction Function)
		{
			return Function.Children.Count > 0;
		}

		protected static string GetEventStructParametersName(UhtType? Outer, string FunctionName)
		{
			string OuterName = String.Empty;
			if (Outer == null)
			{
				throw new UhtIceException("Outer type not set on delegate function");
			}
			else if (Outer is UhtClass Class)
			{
				OuterName = Class.EngineName;
			}
			else if (Outer is UhtHeaderFile)
			{
				string PackageName = Outer.Package.EngineName;
				OuterName = PackageName.Replace('/', '_');
			}

			string Result = $"{OuterName}_event{FunctionName}_Parms";
			if (UhtFCString.IsDigit(Result[0]))
			{
				Result = "_" + Result;
			}
			return Result;
		}
		#endregion

		#region Function helper methods
		protected StringBuilder AppendNativeFunctionHeader(StringBuilder Builder, UhtFunction Function, UhtPropertyTextType TextType, bool bIsDeclaration,
			string? AlternateFunctionName, string? ExtraParam, UhtFunctionExportFlags ExtraExportFlags, string Endl)
		{
			UhtClass? OuterClass = Function.Outer as UhtClass;
			UhtFunctionExportFlags ExportFlags = Function.FunctionExportFlags | ExtraExportFlags;
			bool bIsDelegate = Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate);
			bool bIsInterface = !bIsDelegate && (OuterClass != null && OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Interface));
			bool bIsK2Override = Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent);

			if (!bIsDelegate)
			{
				Builder.Append('\t');
			}

			if (bIsDeclaration)
			{
				// If the function was marked as 'RequiredAPI', then add the *_API macro prefix.  Note that if the class itself
				// was marked 'RequiredAPI', this is not needed as C++ will exports all methods automatically.
				if (TextType != UhtPropertyTextType.EventFunction &&
					!(OuterClass != null && OuterClass.ClassFlags.HasAnyFlags(EClassFlags.RequiredAPI)) &&
					ExportFlags.HasAnyFlags(UhtFunctionExportFlags.RequiredAPI))
				{
					Builder.Append(this.PackageApi);
				}

				if (TextType == UhtPropertyTextType.InterfaceFunction)
				{
					Builder.Append("static ");
				}
				else if (bIsK2Override)
				{
					Builder.Append("virtual ");
				}
				// if the owning class is an interface class
				else if (bIsInterface)
				{
					Builder.Append("virtual ");
				}
				// this is not an event, the function is not a static function and the function is not marked final
				else if (TextType != UhtPropertyTextType.EventFunction && !Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Static) && !ExportFlags.HasAnyFlags(UhtFunctionExportFlags.Final))
				{
					Builder.Append("virtual ");
				}
				else if (ExportFlags.HasAnyFlags(UhtFunctionExportFlags.Inline))
				{
					Builder.Append("inline ");
				}
			}

			UhtProperty? ReturnProperty = Function.ReturnProperty;
			if (ReturnProperty != null)
			{
				if (ReturnProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
				{
					Builder.Append("const ");
				}
				Builder.AppendPropertyText(ReturnProperty, TextType);
			}
			else
			{
				Builder.Append("void");
			}

			Builder.Append(' ');
			if (!bIsDeclaration && OuterClass != null)
			{
				Builder.AppendClassSourceNameOrInterfaceName(OuterClass).Append("::");
			}

			if (AlternateFunctionName != null)
			{
				Builder.Append(AlternateFunctionName);
			}
			else
			{
				switch (TextType)
				{
					case UhtPropertyTextType.InterfaceFunction:
						Builder.Append("Execute_").Append(Function.SourceName);
						break;
					case UhtPropertyTextType.EventFunction:
						Builder.Append(Function.MarshalAndCallName);
						break;
					case UhtPropertyTextType.ClassFunction:
						Builder.Append(Function.CppImplName);
						break;
				}
			}

			AppendParameters(Builder, Function, TextType, ExtraParam, false);

			if (TextType != UhtPropertyTextType.InterfaceFunction)
			{
				if (!bIsDelegate && Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const))
				{
					Builder.Append(" const");
				}

				if (bIsInterface && bIsDeclaration)
				{
					// all methods in interface classes are pure virtuals
					if (bIsK2Override)
					{
						// For BlueprintNativeEvent methods we emit a stub implementation. This allows Blueprints that implement the interface class to be nativized.
						Builder.Append(" {");
						if (ReturnProperty != null)
						{
							if (ReturnProperty is UhtByteProperty ByteProperty && ByteProperty.Enum != null)
							{
								Builder.Append(" return TEnumAsByte<").Append(ByteProperty.Enum.CppType).Append(">(").AppendNullConstructorArg(ReturnProperty, false).Append("); ");
							}
							else
							{
								Builder.Append(" return ").AppendNullConstructorArg(ReturnProperty, false).Append("; ");
							}
						}
						Builder.Append('}');
					}
					else
					{
						Builder.Append("=0");
					}
				}
			}
			Builder.Append(Endl);

			return Builder;
		}

		protected StringBuilder AppendEventFunctionPrologue(StringBuilder Builder, UhtFunction Function, string FunctionName, int Tabs, string Endl)
		{
			Builder.AppendTabs(Tabs).Append("{").Append(Endl);
			if (Function.Children.Count == 0)
			{
				return Builder;
			}

			++Tabs;

			string EventParameterStructName = GetEventStructParametersName(Function.Outer, FunctionName);

			Builder.AppendTabs(Tabs).Append(EventParameterStructName).Append(" Parms;").Append(Endl);

			// Declare a parameter struct for this event/delegate and assign the struct members using the values passed into the event/delegate call.
			foreach (UhtProperty Property in Function.ParameterProperties.Span)
			{
				if (Property.bIsStaticArray)
				{
					Builder.AppendTabs(Tabs).Append("FMemory::Memcpy(Parm.")
						.Append(Property.SourceName).Append(',')
						.Append(Property.SourceName).Append(",sizeof(Parms.")
						.Append(Property.SourceName).Append(");").Append(Endl);
				}
				else
				{
					Builder.AppendTabs(Tabs).Append("Parms.").Append(Property.SourceName).Append("=").Append(Property.SourceName);
					if (Property is UhtBooleanProperty)
					{
						Builder.Append(" ? true : false");
					}
					Builder.Append(";").Append(Endl);
				}
			}
			return Builder;
		}

		protected StringBuilder AppendEventFunctionEpilogue(StringBuilder Builder, UhtFunction Function, int Tabs, string Endl)
		{
			++Tabs;

			// Out parm copying.
			foreach (UhtProperty Property in Function.ParameterProperties.Span)
			{
				if (Property.PropertyFlags.HasExactFlags(EPropertyFlags.ConstParm | EPropertyFlags.OutParm, EPropertyFlags.OutParm))
				{
					if (Property.bIsStaticArray)
					{
						Builder
							.AppendTabs(Tabs)
							.Append("FMemory::Memcpy(&")
							.Append(Property.SourceName)
							.Append(",&Parms.")
							.Append(Property.SourceName)
							.Append(",sizeof(")
							.Append(Property.SourceName)
							.Append("));").Append(Endl);
					}
					else
					{
						Builder
							.AppendTabs(Tabs)
							.Append(Property.SourceName)
							.Append("=Parms.")
							.Append(Property.SourceName)
							.Append(";").Append(Endl);
					}
				}
			}

			// Return value.
			UhtProperty? ReturnProperty = Function.ReturnProperty;
			if (ReturnProperty != null)
			{
				// Make sure uint32 -> bool is supported
				if (ReturnProperty is UhtBooleanProperty)
				{
					Builder
						.AppendTabs(Tabs)
						.Append("return !!Parms.")
						.Append(ReturnProperty.SourceName)
						.Append(";").Append(Endl);
				}
				else
				{
					Builder
						.AppendTabs(Tabs)
						.Append("return Parms.")
						.Append(ReturnProperty.SourceName)
						.Append(";").Append(Endl);
				}
			}

			--Tabs;
			Builder.AppendTabs(Tabs).Append("}").Append(Endl);
			return Builder;
		}

		protected StringBuilder AppendParameters(StringBuilder Builder, UhtFunction Function, UhtPropertyTextType TextType, string? ExtraParameter, bool bSkipParameterName)
		{
			bool bNeedsSeperator = false;

			Builder.Append('(');

			if (ExtraParameter != null)
			{
				Builder.Append(ExtraParameter);
				bNeedsSeperator = true;
			}

			foreach (UhtProperty Parameter in Function.ParameterProperties.Span)
			{
				if (bNeedsSeperator)
				{
					Builder.Append(", ");
				}
				else
				{
					bNeedsSeperator = true;
				}
				Builder.AppendFullDecl(Parameter, TextType, bSkipParameterName);
			}

			Builder.Append(")");
			return Builder;
		}

		protected static bool IsRpcFunction(UhtFunction Function)
		{
			return Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native) &&
				!Function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CustomThunk);
		}

		protected static bool IsRpcFunction(UhtFunction Function, bool bEditorOnly)
		{
			return IsRpcFunction(Function) && Function.FunctionFlags.HasAnyFlags(EFunctionFlags.EditorOnly) == bEditorOnly;
		}

		/// <summary>
		/// Determines whether the glue version of the specified native function should be exported.
		/// </summary>
		/// <param name="Function">The function to check</param>
		/// <returns>True if the glue version of the function should be exported.</returns>
		protected static bool ShouldExportFunction(UhtFunction Function)
		{
			// export any script stubs for native functions declared in interface classes
			bool bIsBlueprintNativeEvent = Function.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native);
			if (!bIsBlueprintNativeEvent)
			{
				if (Function.Outer != null)
				{
					if (Function.Outer is UhtClass Class)
					{
						if (Class.ClassFlags.HasAnyFlags(EClassFlags.Interface))
						{
							return true;
						}
					}
				}
			}

			// always export if the function is static
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Static))
			{
				return true;
			}

			// don't export the function if this is not the original declaration and there is
			// at least one parent version of the function that is declared native
			for (UhtFunction? ParentFunction = Function.SuperFunction; ParentFunction != null; ParentFunction = ParentFunction.SuperFunction)
			{
				if (ParentFunction.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
				{
					return false;
				}
			}
			return true;
		}

		protected static string GetDelegateFunctionExportName(UhtFunction Function)
		{
			const string DelegatePrefix = "delegate";

			if (!Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
			{
				throw new UhtIceException("Attempt to export a function that isn't a delegate as a delegate");
			}
			if (!Function.MarshalAndCallName.StartsWith(DelegatePrefix))
			{
				throw new UhtIceException("Marshal and call name must begin with 'delegate'");
			}
			return $"F{Function.MarshalAndCallName.Substring(DelegatePrefix.Length)}_DelegateWrapper";
		}

		protected static string GetDelegateFunctionExtraParameter(UhtFunction Function)
		{
			string ReturnType = Function.FunctionFlags.HasAnyFlags(EFunctionFlags.MulticastDelegate) ? "FMulticastScriptDelegate" : "FScriptDelegate";
			return $"const {ReturnType}& {Function.SourceName.Substring(1)}";
		}
		#endregion
	}

	internal static class UhtHaederCodeGeneratorStringBuilderExtensions
	{
		public static StringBuilder AppendMacroName(this StringBuilder Builder, string FileId, int LineNumber, string MacroSuffix)
		{
			return Builder.Append(FileId).Append('_').Append(LineNumber).Append('_').Append(MacroSuffix);
		}

		public static StringBuilder AppendMacroName(this StringBuilder Builder, UhtHeaderCodeGenerator Generator, int LineNumber, string MacroSuffix)
		{
			return Builder.AppendMacroName(Generator.FileId, LineNumber, MacroSuffix);
		}

		public static StringBuilder AppendMacroName(this StringBuilder Builder, UhtHeaderCodeGenerator Generator, UhtClass Class, string MacroSuffix)
		{
			return Builder.AppendMacroName(Generator, Class.GeneratedBodyLineNumber, MacroSuffix);
		}

		public static StringBuilder AppendMacroName(this StringBuilder Builder, UhtHeaderCodeGenerator Generator, UhtScriptStruct ScriptStruct, string MacroSuffix)
		{
			return Builder.AppendMacroName(Generator, ScriptStruct.MacroDeclaredLineNumber, MacroSuffix);
		}

		public static StringBuilder AppendMacroName(this StringBuilder Builder, UhtHeaderCodeGenerator Generator, UhtFunction Function, string MacroSuffix)
		{
			return Builder.AppendMacroName(Generator, Function.MacroLineNumber, MacroSuffix);
		}
	}
}
