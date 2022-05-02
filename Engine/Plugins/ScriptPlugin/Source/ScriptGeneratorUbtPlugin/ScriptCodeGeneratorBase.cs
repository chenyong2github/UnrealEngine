// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnrealBuildBase;
using UnrealBuildTool;

namespace ScriptGeneratorUbtPlugin
{
	abstract internal class ScriptCodeGeneratorBase
	{
		public readonly IUhtExportFactory Factory;
		public UhtSession Session => this.Factory.Session;

		public ScriptCodeGeneratorBase(IUhtExportFactory Factory)
		{
			this.Factory = Factory;
		}

		/// <summary>
		/// Export all the classes in all the packages
		/// </summary>
		public void Generate()
		{
			DirectoryReference ConfigDirectory = DirectoryReference.Combine(Unreal.EngineDirectory, "Programs/UnrealBuildTool");
			ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, ConfigDirectory, BuildHostPlatform.Current.Platform);
			Ini.GetArray("Plugins", "ScriptSupportedModules", out List<string>? SupportedScriptModules);

			// Loop through the packages making sure they should be exported.  Queue the export of the classes
			List<UhtClass> Classes = new();
			List<Task?> Tasks = new();
			foreach (UhtPackage Package in this.Session.Packages)
			{
				if (Package.Module.ModuleType != UHTModuleType.EngineRuntime && Package.Module.ModuleType != UHTModuleType.GameRuntime)
				{
					continue;
				}

				if (SupportedScriptModules != null && !SupportedScriptModules.Any(x => string.Compare(x, Package.Module.Name, StringComparison.OrdinalIgnoreCase) == 0))
				{
					continue;
				}

				QueueClassExports(Package, Package, Classes, Tasks);
			}

			// Wait for all the classes to export
			Task[]? WaitTasks = Tasks.Where(x => x != null).Cast<Task>().ToArray();
			if (WaitTasks.Length > 0)
			{
				Task.WaitAll(WaitTasks);
			}

			// Finish the export process
			Finish(Classes);
		}

		/// <summary>
		/// Collect the classes to be exported for the given package and type
		/// </summary>
		/// <param name="Package">Package being exported</param>
		/// <param name="Type">Type to test for exporting</param>
		/// <param name="Classes">Collection of exported classes</param>
		/// <param name="Tasks">Collection of queued tasks</param>
		private void QueueClassExports(UhtPackage Package, UhtType Type, List<UhtClass> Classes, List<Task?> Tasks)
		{
			if (Type is UhtClass Class)
			{
				if (CanExportClass(Class))
				{
					Classes.Add(Class);
					Tasks.Add(Factory.CreateTask((Factory) => { ExportClass(Class); }));
				}
			}
			foreach (UhtType Child in Type.Children)
			{
				QueueClassExports(Package, Child, Classes, Tasks);
			}
		}

		/// <summary>
		/// Test to see if the given class should be exported
		/// </summary>
		/// <param name="Class">Class to test</param>
		/// <returns>True if the class should be exported, false if not</returns>
		protected virtual bool CanExportClass(UhtClass Class)
		{
			return Class.ClassFlags.HasAnyFlags(EClassFlags.RequiredAPI | EClassFlags.MinimalAPI); // Don't export classes that don't export DLL symbols
		}

		/// <summary>
		/// Test to see if the given function should be exported
		/// </summary>
		/// <param name="Class">Owning class of the function</param>
		/// <param name="Function">Function to test</param>
		/// <returns>True if the function should be exported</returns>
		protected virtual bool CanExportFunction(UhtClass Class, UhtFunction Function)
		{
			// We don't support delegates and non-public functions
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
			{
				return false;
			}

			// Reject if any of the parameter types is unsupported yet
			foreach (UhtType Child in Function.Children)
			{
				if (Child is UhtArrayProperty ||
					Child is UhtDelegateProperty ||
					Child is UhtMulticastDelegateProperty ||
					Child is UhtWeakObjectPtrProperty ||
					Child is UhtInterfaceProperty)
				{
					return false;
				}
				if (Child is UhtProperty Property && Property.IsStaticArray)
				{
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Test to see if the given property should be exported
		/// </summary>
		/// <param name="Class">Owning class of the property</param>
		/// <param name="Property">Property to test</param>
		/// <returns>True if the property should be exported</returns>
		protected virtual bool CanExportProperty(UhtClass Class, UhtProperty Property)
		{
			// Property must be DLL exported
			if (!Class.ClassFlags.HasAnyFlags(EClassFlags.RequiredAPI))
			{
				return false;
			}

			// Only public, editable properties can be exported
			if (Property.PropertyFlags.HasAnyFlags(EPropertyFlags.NativeAccessSpecifierPrivate | EPropertyFlags.NativeAccessSpecifierProtected) || 
				!Property.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit))
			{
				return false;
			}

			// Reject if any of the parameter types is unsupported yet
			if (Property.IsStaticArray ||
				Property is UhtArrayProperty ||
				Property is UhtDelegateProperty ||
				Property is UhtMulticastDelegateProperty ||
				Property is UhtWeakObjectPtrProperty ||
				Property is UhtInterfaceProperty ||
				Property is UhtStructProperty)
			{
				return false;
			}
			return true;
		}

		/// <summary>
		/// Export the given class
		/// </summary>
		/// <param name="Factory">Factory associated with the export</param>
		/// <param name="Class">Class to export</param>
		private void ExportClass(UhtClass Class)
		{
			using BorrowStringBuilder Borrower = new(StringBuilderCache.Big);
			ExportClass(Borrower.StringBuilder, Class);
			string FileName = this.Factory.MakePath(Class.EngineName, ".script.h");
			this.Factory.CommitOutput(FileName, Borrower.StringBuilder);
		}

		private void Finish(List<UhtClass> Classes)
		{
			using BorrowStringBuilder Borrower = new(StringBuilderCache.Big);
			Finish(Borrower.StringBuilder, Classes);
			string FileName = this.Factory.MakePath("GeneratedScriptLibraries", ".inl");
			this.Factory.CommitOutput(FileName, Borrower.StringBuilder);
		}

		abstract protected void ExportClass(StringBuilder Builder, UhtClass Class);

		abstract protected void Finish(StringBuilder Builder, List<UhtClass> Classes);

		virtual protected StringBuilder AppendInitializeFunctionDispatchParam(StringBuilder Builder, UhtClass Class, UhtFunction? Function, UhtProperty Property, int PropertyIndex)
		{
			if (Property is UhtObjectPropertyBase)
			{
				Builder.Append("NULL");
			}
			else
			{
				Builder.AppendPropertyText(Property, UhtPropertyTextType.GenericFunctionArgOrRetVal).Append("()");
			}
			return Builder;
		}

		virtual protected StringBuilder AppendFunctionDispatch(StringBuilder Builder, UhtClass Class, UhtFunction Function)
		{
			bool bHasParamsOrReturnValue = Function.Children.Count > 0;
			if (bHasParamsOrReturnValue)
			{
				Builder.Append("\tstruct FDispatchParams\r\n");
				Builder.Append("\t{\r\n");
				foreach (UhtProperty Property in Function.Children)
				{
					Builder.Append("\t\t").AppendPropertyText(Property, UhtPropertyTextType.GenericFunctionArgOrRetVal).Append(' ').Append(Property.SourceName).Append(";\r\n");
				}
				Builder.Append("\t} Params;\r\n");
				int PropertyIndex = 0;
				foreach (UhtProperty Property in Function.Children)
				{
					Builder.Append("\tParams.").Append(Property.SourceName).Append(" = ");
					AppendInitializeFunctionDispatchParam(Builder, Class, Function, Property, PropertyIndex).Append(";\r\n");
					PropertyIndex++;
				}
			}

			Builder.Append("\tstatic UFunction* Function = Obj->FindFunctionChecked(TEXT(\"").Append(Function.SourceName).Append("\"));\r\n");

			if (bHasParamsOrReturnValue)
			{
				Builder.Append("\tcheck(Function->ParmsSize == sizeof(FDispatchParams));\r\n");
				Builder.Append("\tObj->ProcessEvent(Function, &Params);\r\n");
			}
			else
			{
				Builder.Append("\tObj->ProcessEvent(Function, NULL);\r\n");
			}
			return Builder;
		}
	}
}
