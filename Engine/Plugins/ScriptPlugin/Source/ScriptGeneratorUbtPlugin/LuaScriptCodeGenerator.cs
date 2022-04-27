// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace ScriptGeneratorUbtPlugin
{
	static class LuaScriptCodeGeneratorStringBuilderExtensinos
	{
		public static StringBuilder AppendLuaWrapperFunctionDeclaration(this StringBuilder Builder, UhtClass Class, string FunctionName)
		{
			return Builder.Append("int32 ").Append(Class.EngineName).Append('_').Append(FunctionName).Append("(lua_State* InScriptContext)");
		}

		public static StringBuilder AppendLuaObjectDeclarationFromContext(this StringBuilder Builder, UhtClass Class)
		{
			return Builder.Append("UObject* Obj = (").Append(Class.SourceName).Append("*)lua_touserdata(InScriptContext, 1);");
		}

		public static StringBuilder AppendLuaReturnValueHandler(this StringBuilder Builder, UhtClass Class, UhtProperty? ReturnValue, string? ReturnValueName)
		{
			if (ReturnValue != null)
			{
				if (ReturnValue is UhtIntProperty)
				{
					Builder.Append("lua_pushinteger(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
				}
				else if (ReturnValue is UhtFloatProperty)
				{
					Builder.Append("lua_pushnumber(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
				}
				else if (ReturnValue is UhtStrProperty)
				{
					Builder.Append("lua_pushstring(InScriptContext, TCHAR_TO_ANSI(*").Append(ReturnValueName).Append("));\r\n");
				}
				else if (ReturnValue is UhtNameProperty)
				{
					Builder.Append("lua_pushstring(InScriptContext, TCHAR_TO_ANSI(*").Append(ReturnValueName).Append(".ToString()));\r\n");
				}
				else if (ReturnValue is UhtBoolProperty)
				{
					Builder.Append("lua_pushboolean(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
				}
				else if (ReturnValue is UhtStructProperty StructProperty)
				{
					if (StructProperty.ScriptStruct.EngineName == "Vector2D")
					{
						Builder.Append("FLuaVector2D::Return(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Vector")
					{
						Builder.Append("FLuaVector::Return(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Vector4")
					{
						Builder.Append("FLuaVector4::Return(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Quat")
					{
						Builder.Append("FLuaQuat::Return(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
					}
					else if (StructProperty.ScriptStruct.EngineName == "LinearColor")
					{
						Builder.Append("FLuaLinearColor::Return(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Color")
					{
						Builder.Append("FLuaLinearColor::Return(InScriptContext, FLinearColor(").Append(ReturnValueName).Append("));\r\n");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Transform")
					{
						Builder.Append("FLuaTransform::Return(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
					}
					else
					{
						throw new UhtIceException($"Unsupported function return value struct type: {StructProperty.ScriptStruct.EngineName}");
					}
				}
				else if (ReturnValue is UhtObjectPropertyBase)
				{
					Builder.Append("lua_pushlightuserdata(InScriptContext, ").Append(ReturnValueName).Append(");\r\n");
				}
				else
				{
					throw new UhtIceException($"Unsupported function return type: {ReturnValue.GetType().Name}");
				}
				Builder.Append("\treturn 1;");
			}
			else
			{
				Builder.Append("return 0;");
			}
			return Builder;
		}
	}

	internal class LuaScriptCodeGenerator : ScriptCodeGeneratorBase
	{
		public LuaScriptCodeGenerator(IUhtExportFactory Factory)
			: base(Factory)
		{
		}

		protected override bool CanExportClass(UhtClass Class)
		{
			if (!base.CanExportClass(Class))
			{
				return false;
			}

			for (UhtClass? Current = Class; Current != null; Current = Current.SuperClass)
			{
				foreach (UhtType Child in Current.Children)
				{
					if (Child is UhtFunction Function)
					{
						if (CanExportFunction(Class, Function))
						{
							return true;
						}
					}
				}
			}

			foreach (UhtType Child in Class.Children)
			{
				if (Child is UhtProperty Property)
				{
					if (CanExportProperty(Class, Property))
					{
						return true;
					}
				}
			}
			return false;
		}

		protected override bool CanExportFunction(UhtClass Class, UhtFunction Function)
		{
			if (!base.CanExportFunction(Class, Function))
			{
				return false;
			}

			foreach (UhtType Child in Function.Children)
			{
				if (Child is UhtProperty Property)
				{
					if (!IsPropertyTypeSupported(Property))
					{
						return false;
					}
				}
			}
			return true;
		}

		protected override bool CanExportProperty(UhtClass Class, UhtProperty Property)
		{
			return Property.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit) && IsPropertyTypeSupported(Property);
		}

		private bool IsPropertyTypeSupported(UhtProperty Property)
		{
			if (Property is UhtStructProperty StructProperty)
			{
				return
					StructProperty.ScriptStruct.EngineName == "Vector2D" ||
					StructProperty.ScriptStruct.EngineName == "Vector" ||
					StructProperty.ScriptStruct.EngineName == "Vector4" ||
					StructProperty.ScriptStruct.EngineName == "Quat" ||
					StructProperty.ScriptStruct.EngineName == "LinearColor" ||
					StructProperty.ScriptStruct.EngineName == "Color" ||
					StructProperty.ScriptStruct.EngineName == "Transform";
			}
			else if (
				Property is UhtLazyObjectPtrProperty ||
				Property is UhtSoftObjectProperty ||
				Property is UhtSoftClassProperty ||
				Property is UhtWeakObjectPtrProperty)
			{
				return false;
			}
			else if (
				Property is UhtIntProperty ||
				Property is UhtFloatProperty ||
				Property is UhtStrProperty ||
				Property is UhtNameProperty ||
				Property is UhtBoolProperty ||
				Property is UhtObjectPropertyBase)
			{
				return true;
			}
			return false;
		}

		protected override void ExportClass(StringBuilder Builder, UhtClass Class)
		{
			Builder.Append("#pragma once\r\n\r\n");

			List<UhtFunction> Functions = Class.Children.Where(x => x is UhtFunction).Cast<UhtFunction>().Reverse().ToList();

			//ETSTODO - Functions are reversed in the engine
			for (UhtClass? Current = Class; Current != null; Current = Current.SuperClass)
			{
				foreach (UhtFunction Function in Current.Functions.Reverse())
				{
					if (CanExportFunction(Class, Function))
					{
						ExportFunction(Builder, Class, Function);
					}
				}
			}

			for (UhtClass? Current = Class; Current != null; Current = Current.SuperClass)
			{
				foreach (UhtType Child in Current.Children)
				{
					if (Child is UhtProperty Property && CanExportProperty(Class, Property))
					{
						ExportProperty(Builder, Class, Property);
					}
				}
			}

			if (!Class.ClassFlags.HasAnyFlags(EClassFlags.Abstract))
			{
				Builder.AppendLuaWrapperFunctionDeclaration(Class, "New").Append("\r\n");
				Builder.Append("{\r\n");
				Builder.Append("\tUObject* Outer = (UObject*)lua_touserdata(InScriptContext, 1);\r\n");
				Builder.Append("\tFName Name = FName(luaL_checkstring(InScriptContext, 2));\r\n");
				Builder.Append("\tUObject* Obj = NewObject<").Append(Class.SourceName).Append(">(Outer, Name);\r\n");
				Builder.Append("\tif (Obj)\r\n\t{\r\n");
				Builder.Append("\t\tFScriptObjectReferencer::Get().AddObjectReference(Obj);\r\n");
				Builder.Append("\t}\r\n");
				Builder.Append("\tlua_pushlightuserdata(InScriptContext, Obj);\r\n");
				Builder.Append("\treturn 1;\r\n");
				Builder.Append("}\r\n\r\n");

				Builder.AppendLuaWrapperFunctionDeclaration(Class, "Destroy").Append("\r\n");
				Builder.Append("{\r\n");
				Builder.Append("\t").AppendLuaObjectDeclarationFromContext(Class).Append("\r\n");
				Builder.Append("\tif (Obj)\r\n\t{\r\n");
				Builder.Append("\t\tFScriptObjectReferencer::Get().RemoveObjectReference(Obj);\r\n");
				Builder.Append("\t}\r\n");
				Builder.Append("\treturn 0;\r\n");
				Builder.Append("}\r\n\r\n");
			}

			// Class: Equivalent of StaticClass()
			Builder.AppendLuaWrapperFunctionDeclaration(Class, "Class").Append("\r\n");
			Builder.Append("{\r\n");
			Builder.Append("\tUClass* Class = ").Append(Class.SourceName).Append("::StaticClass();\r\n");
			Builder.Append("\tlua_pushlightuserdata(InScriptContext, Class);\r\n");
			Builder.Append("\treturn 1;\r\n");
			Builder.Append("}\r\n\r\n");

			// Library
			Builder.Append("static const luaL_Reg ").Append(Class.EngineName).Append("_Lib[] =\r\n");
			Builder.Append("{\r\n");
			if (!Class.ClassFlags.HasAnyFlags(EClassFlags.Abstract))
			{
				Builder.Append("\t{ \"New\", ").Append(Class.EngineName).Append("_New },\r\n");
				Builder.Append("\t{ \"Destroy\", ").Append(Class.EngineName).Append("_Destroy },\r\n");
				Builder.Append("\t{ \"Class\", ").Append(Class.EngineName).Append("_Class },\r\n");
			}

			//ETSTODO - Functions are reversed in the engine
			for (UhtClass? Current = Class; Current != null; Current = Current.SuperClass)
			{
				foreach (UhtFunction Function in Current.Functions.Reverse())
				{
					if (CanExportFunction(Class, Function))
					{
						Builder.Append("\t{ \"").Append(Function.SourceName).Append("\", ").Append(Class.EngineName).Append('_').Append(Function.SourceName).Append(" },\r\n");
					}
				}
			}

			for (UhtClass? Current = Class; Current != null; Current = Current.SuperClass)
			{
				foreach (UhtType Child in Current.Children)
				{
					if (Child is UhtProperty Property && CanExportProperty(Class, Property))
					{
						Builder.Append("\t{ \"Get_").Append(Property.SourceName).Append("\", ").Append(Class.EngineName).Append("_Get_").Append(Property.SourceName).Append(" },\r\n");
						Builder.Append("\t{ \"Set_").Append(Property.SourceName).Append("\", ").Append(Class.EngineName).Append("_Set_").Append(Property.SourceName).Append(" },\r\n");
					}
				}
			}

			Builder.Append("\t{ NULL, NULL }\r\n");
			Builder.Append("};\r\n\r\n");
		}

		protected void ExportFunction(StringBuilder Builder, UhtClass Class, UhtFunction Function)
		{
			UhtClass? FunctionSuper = null;
			if (Function.Outer != null && Function.Outer != Class && Function.Outer is UhtClass OuterClass && base.CanExportClass(OuterClass))
			{
				FunctionSuper = OuterClass;
			}

			Builder.AppendLuaWrapperFunctionDeclaration(Class, Function.SourceName).Append("\r\n");
			Builder.Append("{\r\n");
			if (FunctionSuper == null)
			{
				Builder.Append("\t").AppendLuaObjectDeclarationFromContext(Class).Append("\r\n");
				AppendFunctionDispatch(Builder, Class, Function);
				string ReturnValueName = Function.ReturnProperty != null ? $"Params.{Function.ReturnProperty.SourceName}" : String.Empty;
				Builder.Append("\t").AppendLuaReturnValueHandler(Class, Function.ReturnProperty, ReturnValueName).Append("\r\n");
			}
			else
			{
				Builder.Append("\treturn ").Append(FunctionSuper.EngineName).Append('_').Append(Function.SourceName).Append("(InScriptContext);\r\n");
			}
			Builder.Append("}\r\n\r\n");
		}

		protected void ExportProperty(StringBuilder Builder, UhtClass Class, UhtProperty Property)
		{
			UhtClass? PropertySuper = null;
			if (Property.Outer != null && Property.Outer != Class && Property.Outer is UhtClass OuterClass && base.CanExportClass(OuterClass))
			{
				PropertySuper = OuterClass;
			}

			// Getter
			Builder.AppendLuaWrapperFunctionDeclaration(Class, $"Get_{Property.SourceName}").Append("\r\n");
			Builder.Append("{\r\n");
			if (PropertySuper == null)
			{
				Builder.Append('\t').AppendLuaObjectDeclarationFromContext(Class).Append("\r\n");
				Builder.Append("\tstatic FProperty* Property = FindScriptPropertyHelper(").Append(Class.SourceName).Append("::StaticClass(), TEXT(\"").Append(Property.SourceName).Append("\"));\r\n");
				Builder.Append('\t').AppendPropertyText(Property, UhtPropertyTextType.GenericFunctionArgOrRetVal).Append(" PropertyValue;\r\n");
				Builder.Append("\tProperty->CopyCompleteValue(&PropertyValue, Property->ContainerPtrToValuePtr<void>(Obj));\r\n");
				Builder.Append('\t').AppendLuaReturnValueHandler(Class, Property, "PropertyValue").Append("\r\n");
			}
			else
			{
				Builder.Append("\treturn ").Append(PropertySuper.EngineName).Append('_').Append("Get_").Append(Property.SourceName).Append("(InScriptContext);\r\n");
			}
			Builder.Append("}\r\n\r\n");

			// Setter
			Builder.AppendLuaWrapperFunctionDeclaration(Class, $"Set_{Property.SourceName}").Append("\r\n");
			Builder.Append("{\r\n");
			if (PropertySuper == null)
			{
				Builder.Append('\t').AppendLuaObjectDeclarationFromContext(Class).Append("\r\n");
				Builder.Append("\tstatic FProperty* Property = FindScriptPropertyHelper(").Append(Class.SourceName).Append("::StaticClass(), TEXT(\"").Append(Property.SourceName).Append("\"));\r\n");
				Builder.Append('\t').AppendPropertyText(Property, UhtPropertyTextType.GenericFunctionArgOrRetVal).Append(" PropertyValue = ");
				AppendInitializeFunctionDispatchParam(Builder, Class, null, Property, 0).Append(";\r\n");
				Builder.Append("\tProperty->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Obj), &PropertyValue);\r\n");
				Builder.Append("\treturn 0;\r\n");
			}
			else
			{
				Builder.Append("\treturn ").Append(PropertySuper.EngineName).Append('_').Append("Set_").Append(Property.SourceName).Append("(InScriptContext);\r\n");
			}
			Builder.Append("}\r\n\r\n");
		}

		protected override void Finish(StringBuilder Builder, List<UhtClass> Classes)
		{
			HashSet<UhtHeaderFile> UniqueHeaders = new HashSet<UhtHeaderFile>();
			foreach (UhtClass Class in Classes)
			{
				UniqueHeaders.Add(Class.HeaderFile);
			}
			List<string> SortedHeaders = new List<string>();
			foreach (UhtHeaderFile HeaderFile in UniqueHeaders)
			{
				SortedHeaders.Add(HeaderFile.FilePath);
			}
			SortedHeaders.Sort(StringComparerUE.OrdinalIgnoreCase);
			foreach (string FilePath in SortedHeaders)
			{
				string RelativePath = Path.GetRelativePath(this.Factory.PluginModule!.IncludeBase, FilePath).Replace("\\", "/");
				Builder.Append("#include \"").Append(RelativePath).Append("\"\r\n");
			}

			Classes.Sort((x, y) => StringComparerUE.OrdinalIgnoreCase.Compare(x.EngineName, y.EngineName));
			SortedHeaders.Clear();
			foreach (UhtClass Class in Classes)
			{
				Builder.Append("#include \"").Append(Class.EngineName).Append(".script.h\"\r\n");
			}
			Builder.Append("\r\n");
			Builder.Append("void LuaRegisterExportedClasses(lua_State* InScriptContext)\r\n");
			Builder.Append("{\r\n");
			foreach(UhtClass Class in Classes)
			{
				Builder.Append("\tFLuaUtils::RegisterLibrary(InScriptContext, ").Append(Class.EngineName).Append("_Lib, \"").Append(Class.EngineName).Append("\");\r\n");
			}
			Builder.Append("}\r\n\r\n");
		}

		protected override StringBuilder AppendInitializeFunctionDispatchParam(StringBuilder Builder, UhtClass Class, UhtFunction? Function, UhtProperty Property, int PropertyIndex)
		{
			if (!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm))
			{
				int ParamIndex = PropertyIndex + 2;
				if (Property is UhtIntProperty)
				{
					Builder.Append("(luaL_checkint");
				}
				else if (Property is UhtFloatProperty)
				{
					Builder.Append("(float)(luaL_checknumber");
				}
				else if (Property is UhtStrProperty)
				{
					Builder.Append("ANSI_TO_TCHAR(luaL_checkstring");
				}
				else if (Property is UhtNameProperty)
				{
					Builder.Append("FName(luaL_checkstring");
				}
				else if (Property is UhtBoolProperty)
				{
					Builder.Append("!!(lua_toboolean");
				}
				else if (Property is UhtStructProperty StructProperty)
				{
					if (StructProperty.ScriptStruct.EngineName == "Vector2D")
					{
						Builder.Append("(FLuaVector2D::Get");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Vector")
					{
						Builder.Append("(FLuaVector::Get");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Vector4")
					{
						Builder.Append("(FLuaVector4::Get");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Quat")
					{
						Builder.Append("(FLuaQuat::Get");
					}
					else if (StructProperty.ScriptStruct.EngineName == "LinearColor")
					{
						Builder.Append("(FLuaLinearColor::Get");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Color")
					{
						Builder.Append("FColor(FLuaLinearColor::Get");
					}
					else if (StructProperty.ScriptStruct.EngineName == "Transform")
					{
						Builder.Append("(FLuaTransform::Get");
					}
					else
					{
						throw new UhtIceException($"Unsupported function param struct type: {StructProperty.ScriptStruct.EngineName}");
					}
				}
				else if (Property is UhtClassProperty)
				{
					Builder.Append("(UClass*)(lua_touserdata");
				}
				else if (Property is UhtObjectPropertyBase)
				{
					Builder.Append("(").AppendPropertyText(Property, UhtPropertyTextType.GenericFunctionArgOrRetVal).Append(")(lua_touserdata");
				}
				else
				{
					throw new UhtIceException($"Unsupported function param type: {Property.GetType().Name}");
				}
				Builder.Append("(InScriptContext, ").Append(ParamIndex).Append("))");
			}
			else
			{
				base.AppendInitializeFunctionDispatchParam(Builder, Class, Function, Property, PropertyIndex);
			}
			return Builder;
		}
	}
}
