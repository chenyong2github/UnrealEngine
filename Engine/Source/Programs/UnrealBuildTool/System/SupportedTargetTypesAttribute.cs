// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	/// <summary>
	/// Attribute which can be applied to a TargetRules-dervied class to indicate which target types it supports
	/// </summary>
	[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
	public class SupportedTargetTypesAttribute : Attribute
	{
		/// <summary>
		/// Array of supported target types
		/// </summary>
		public readonly TargetType[] TargetTypes;

		/// <summary>
		/// Initialize the attribute with a list of target types
		/// </summary>
		/// <param name="TargetTypes">Variable-length array of target type arguments</param>
		public SupportedTargetTypesAttribute(params TargetType[] TargetTypes)
		{
			this.TargetTypes = TargetTypes;
		}
	}
}
