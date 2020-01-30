// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Run
{
	/// <summary>
	/// Attribute used to indicate that an ErrorMatcher class should be created and registered automatically at startup
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	class AutoRegisterAttribute : Attribute
	{
	}
}
