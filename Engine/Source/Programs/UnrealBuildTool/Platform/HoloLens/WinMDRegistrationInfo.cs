// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
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
			/// <param name="InThreadingModelName"></param>
			public ActivatableType(string InTypeName, string InThreadingModelName)
			{
				TypeName = InTypeName;
				ThreadingModelName = InThreadingModelName;
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

			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				ActivatableTypesList = new List<ActivatableType>();

				Assembly DependsOn = Assembly.ReflectionOnlyLoadFrom(InWindMDSourcePath.FullName);
				foreach (Type WinMDType in DependsOn.GetExportedTypes())
				{
					bool IsActivatable = false;
					string ThreadingModelName = "both";
					foreach (CustomAttributeData Attr in WinMDType.CustomAttributes)
					{
						if (Attr.AttributeType.FullName == "Windows.Foundation.Metadata.ActivatableAttribute" || Attr.AttributeType.FullName == "Windows.Foundation.Metadata.StaticAttribute")
						{
							IsActivatable = true;
						}
						else if (Attr.AttributeType.FullName == "Windows.Foundation.Metadata.ThreadingAttribute")
						{
							CustomAttributeTypedArgument Argument = Attr.ConstructorArguments[0];
							ThreadingModelName = Enum.GetName(Argument.ArgumentType, Argument.Value).ToLowerInvariant();
						}
					}
					if (IsActivatable)
					{
						ActivatableTypesList.Add(new ActivatableType(WinMDType.FullName, ThreadingModelName));
					}
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
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
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
		}

		private static List<string> ResolveSearchPaths = new List<string>();
		private List<ActivatableType> ActivatableTypesList;
	}
}
