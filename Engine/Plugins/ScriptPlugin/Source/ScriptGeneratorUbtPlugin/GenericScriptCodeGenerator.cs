// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace ScriptGeneratorUbtPlugin
{
	static class GenericScriptCodeGeneratorStringBuilderExtensinos
	{
		public static StringBuilder AppendGenericWrapperFunctionDeclaration(this StringBuilder Builder, UhtClass Class, string FunctionName)
		{
			return Builder.Append("int32 ").Append(Class.EngineName).Append('_').Append(FunctionName).Append("(void* InScriptContext)");
		}

		public static StringBuilder AppendGenericFunctionParamDeclaration(this StringBuilder Builder, UhtClass Class, UhtProperty Property)
		{
			if (Property is UhtObjectPropertyBase)
			{
				Builder.Append("UObject* ").Append(Property.SourceName).Append(" = nullptr;");
			}
			else
			{
				Builder
					.AppendPropertyText(Property, UhtPropertyTextType.GenericFunctionArgOrRetVal)
					.Append(' ')
					.Append(Property.SourceName)
					.Append(" = ").AppendPropertyText(Property, UhtPropertyTextType.GenericFunctionArgOrRetVal)
					.Append("();");
			}
			return Builder;
		}

		public static StringBuilder AppendGenericObjectDeclarationFromContext(this StringBuilder Builder, UhtClass Class)
		{
			return Builder.Append("UObject* Obj = (UObject*)InScriptContext;");
		}

		public static StringBuilder AppendGenericReturnValueHandler(this StringBuilder Builder, UhtClass Class, UhtProperty? ReturnValue)
		{
			return Builder.Append("return 0;");
		}
	}

	internal class GenericScriptCodeGenerator : ScriptCodeGeneratorBase
	{
		public GenericScriptCodeGenerator(IUhtExportFactory Factory)
			: base(Factory)
		{
		}

		protected override bool CanExportClass(UhtClass Class)
		{
			if (!base.CanExportClass(Class))
			{
				return false;
			}

			foreach (UhtType Child in Class.Children)
			{
				if (Child is UhtFunction Function)
				{
					if (CanExportFunction(Class, Function))
					{
						return true;
					}
				}
				else if (Child is UhtProperty Property)
				{
					if (CanExportProperty(Class, Property))
					{
						return true;
					}
				}
			}
			return false;
		}

		protected override void ExportClass(StringBuilder Builder, UhtClass Class)
		{
			Builder.Append("#pragma once\r\n\r\n");

			List<UhtFunction> Functions = Class.Children.Where(x => x is UhtFunction).Cast<UhtFunction>().Reverse().ToList();

			//ETSTODO - Functions are reversed in the engine
			foreach (UhtFunction Function in Class.Functions.Reverse())
			{
				if (CanExportFunction(Class, Function))
				{
					ExportFunction(Builder, Class, Function);
				}
			}
			//foreach (UhtType Child in Class.Children)
			//{
			//	if (Child is UhtFunction Function && CanExportFunction(Class, Function))
			//	{
			//		ExportFunction(Builder, Class, Function);
			//	}
			//}

			foreach (UhtType Child in Class.Children)
			{
				if (Child is UhtProperty Property && CanExportProperty(Class, Property))
				{
					ExportProperty(Builder, Class, Property);
				}
			}

			if (!Class.ClassFlags.HasAnyFlags(EClassFlags.Abstract))
			{
				Builder.AppendGenericWrapperFunctionDeclaration(Class, "New").Append("\r\n");
				Builder.Append("{\r\n");
				Builder.Append("\tUObject* Outer = NULL;\r\n");
				Builder.Append("\tFName Name = FName(\"ScriptObject\");\r\n");
				Builder.Append("\tUObject* Obj = NewObject<").Append(Class.SourceName).Append(">(Outer, Name);\r\n");
				Builder.Append("\tif (Obj)\r\n\t{\r\n");
				Builder.Append("\t\tFScriptObjectReferencer::Get().AddObjectReference(Obj);\r\n");
				Builder.Append("\t\t// @todo: Register the object with the script context here\r\n");
				Builder.Append("\t}\r\n\treturn 0;\r\n");
				Builder.Append("}\r\n\r\n");

				Builder.AppendGenericWrapperFunctionDeclaration(Class, "Destroy").Append("\r\n");
				Builder.Append("{\r\n");
				Builder.Append("\t").AppendGenericObjectDeclarationFromContext(Class).Append("\r\n");
				Builder.Append("\tif (Obj)\r\n\t{\r\n");
				Builder.Append("\t\tFScriptObjectReferencer::Get().RemoveObjectReference(Obj);\r\n");
				Builder.Append("\t\t// @todo: Remove the object from the script context here if required\r\n");
				Builder.Append("\t}\r\n\treturn 0;\r\n");
				Builder.Append("}\r\n\r\n");
			}
		}

		protected void ExportFunction(StringBuilder Builder, UhtClass Class, UhtFunction Function)
		{
			Builder.AppendGenericWrapperFunctionDeclaration(Class, Function.SourceName).Append("\r\n");
			Builder.Append("{\r\n");
			Builder.Append("\t").AppendGenericObjectDeclarationFromContext(Class).Append("\r\n");
			AppendFunctionDispatch(Builder, Class, Function);
			Builder.Append("\t").AppendGenericReturnValueHandler(Class, Function.ReturnProperty).Append("\r\n");
			Builder.Append("}\r\n\r\n");
		}

		protected void ExportProperty(StringBuilder Builder, UhtClass Class, UhtProperty Property)
		{
			// Getter
			Builder.AppendGenericWrapperFunctionDeclaration(Class, $"Get_{Property.SourceName}").Append("\r\n");
			Builder.Append("{\r\n");
			Builder.Append('\t').AppendGenericObjectDeclarationFromContext(Class).Append("\r\n");
			Builder.Append("\tstatic FProperty* Property = FindScriptPropertyHelper(").Append(Class.SourceName).Append("::StaticClass(), TEXT(\"").Append(Property.SourceName).Append("\"));\r\n");
			Builder.Append('\t').AppendGenericFunctionParamDeclaration(Class, Property).Append("\r\n");
			Builder.Append("\tProperty->CopyCompleteValue(&").Append(Property.SourceName).Append(", Property->ContainerPtrToValuePtr<void>(Obj));\r\n");
			Builder.Append("\t// @todo: handle property value here\r\n");
			Builder.Append("\treturn 0;\r\n");
			Builder.Append("}\r\n\r\n");

			// Setter
			Builder.AppendGenericWrapperFunctionDeclaration(Class, $"Set_{Property.SourceName}").Append("\r\n");
			Builder.Append("{\r\n");
			Builder.Append('\t').AppendGenericObjectDeclarationFromContext(Class).Append("\r\n");
			Builder.Append("\tstatic FProperty* Property = FindScriptPropertyHelper(").Append(Class.SourceName).Append("::StaticClass(), TEXT(\"").Append(Property.SourceName).Append("\"));\r\n");
			Builder.Append('\t').AppendGenericFunctionParamDeclaration(Class, Property).Append("\r\n");
			Builder.Append("\tProperty->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Obj), &").Append(Property.SourceName).Append(");\r\n");
			Builder.Append("\treturn 0;\r\n");
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

			SortedHeaders.Clear();
			foreach (UhtClass Class in Classes)
			{
				SortedHeaders.Add($"{Class.EngineName}.script.h");
			}
			SortedHeaders.Sort(StringComparerUE.OrdinalIgnoreCase);
			foreach (string FilePath in SortedHeaders)
			{
				Builder.Append("#include \"").Append(FilePath).Append("\"\r\n");
			}
		}
	}
}
