// Copyright Epic Games, Inc. All Rights Reserved.

namespace EpicGames.UHT.Types
{
	public abstract class UhtField : UhtObject
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "Field"; }

		public UhtField(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}
	}
}
