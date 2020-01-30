// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon.Perforce;

namespace BuildAgent.Issues
{
	/// <summary>
	/// Stores information about a change and its merge history
	/// </summary>
	class ChangeInfo
	{
		public ChangesRecord Record;
		public List<int> SourceChanges = new List<int>();
		public DescribeRecord CachedDescribeRecord;
	}
}
