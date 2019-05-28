// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using Windows.Foundation.Metadata;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Reflection;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// WinMD registration helper
	/// </summary>
	public class WinMDRegistrationInfo
	{
		/// <summary>
		/// WinMD type info
		/// </summary>
		public class ActivatableType
		{
			/// <summary>
			/// WinMD type info constructor
			/// </summary>
			/// <param name="InTypeName"></param>
			/// <param name="InThreadingModel"></param>
			public ActivatableType(string InTypeName, object InThreadingModel)
			{
				TypeName = InTypeName;
				ThreadingModelName = ((ThreadingModel)InThreadingModel).ToString().ToLowerInvariant();
			}
			
			/// <summary>
			/// Type Name
			/// </summary>
			public string TypeName { get; private set; }
			
			/// <summary>
			/// Threading model
			/// </summary>
			public string ThreadingModelName { get; private set; }
		}

		/// <summary>
		/// WinMD reference info
		/// </summary>
		/// <param name="InWindMDSourcePath"></param>
		/// <param name="InPackageRelativeDllPath"></param>
		public WinMDRegistrationInfo(FileReference InWindMDSourcePath, string InPackageRelativeDllPath)
		{
			PackageRelativeDllPath = InPackageRelativeDllPath;
			ResolveSearchPaths.Add(InWindMDSourcePath.Directory.FullName);

			ActivatableTypesList = new List<ActivatableType>();
			var DependsOn = Assembly.ReflectionOnlyLoadFrom(InWindMDSourcePath.FullName);
			foreach (var WinMDType in DependsOn.GetExportedTypes())
			{
				bool IsActivatable = false;
				object ThreadingModel = Windows.Foundation.Metadata.ThreadingModel.Both;
				foreach (var Attr in WinMDType.CustomAttributes)
				{
					if (Attr.AttributeType.AssemblyQualifiedName == typeof(ActivatableAttribute).AssemblyQualifiedName ||
						Attr.AttributeType.AssemblyQualifiedName == typeof(StaticAttribute).AssemblyQualifiedName)
					{
						IsActivatable = true;
					}
					else if (Attr.AttributeType.AssemblyQualifiedName == typeof(ThreadingAttribute).AssemblyQualifiedName)
					{
						ThreadingModel = Attr.ConstructorArguments[0].Value;
					}
				}
				if (IsActivatable)
				{
					ActivatableTypesList.Add(new ActivatableType(WinMDType.FullName, ThreadingModel));
				}
			}
		}

		/// <summary>
		/// Path to the WinRT library
		/// </summary>
		public string PackageRelativeDllPath { get; private set; }

		/// <summary>
		/// List of the types in the WinMD
		/// </summary>
		public IEnumerable<ActivatableType> ActivatableTypes
		{
			get
			{
				return ActivatableTypesList;
			}
		}

		static WinMDRegistrationInfo()
		{
			AppDomain.CurrentDomain.ReflectionOnlyAssemblyResolve += (Sender, EventArgs) => Assembly.ReflectionOnlyLoad(EventArgs.Name);
			WindowsRuntimeMetadata.ReflectionOnlyNamespaceResolve += (Sender, EventArgs) =>
			{
				string Path = WindowsRuntimeMetadata.ResolveNamespace(EventArgs.NamespaceName, ResolveSearchPaths).FirstOrDefault();
				if (Path == null)
				{
					return;
				}
				EventArgs.ResolvedAssemblies.Add(Assembly.ReflectionOnlyLoadFrom(Path));
			};
		}

		private static List<string> ResolveSearchPaths = new List<string>();
		private List<ActivatableType> ActivatableTypesList;
	}
}
