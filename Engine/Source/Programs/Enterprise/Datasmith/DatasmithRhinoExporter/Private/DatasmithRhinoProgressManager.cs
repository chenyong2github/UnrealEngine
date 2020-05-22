// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace DatasmithRhino
{
	public class FDatasmithRhinoProgressManager : IDisposable
	{
		public FDatasmithRhinoProgressManager(uint DocSerial)
		{
			DocumentSerialNumber = DocSerial;
			bCreatedStatusBar = Rhino.UI.StatusBar.ShowProgressMeter(DocSerial, 0, 1, "Exporting datasmith file", true, true) == 1;
		}

		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (!this.bDisposed)
			{
				if (bCreatedStatusBar)
				{
					Rhino.UI.StatusBar.HideProgressMeter(DocumentSerialNumber);
				}

				bDisposed = true;
			}
		}

		~FDatasmithRhinoProgressManager()
		{
			Dispose(false);
		}

		private uint DocumentSerialNumber;
		private bool bCreatedStatusBar = false;
		private bool bDisposed = false;
	}
}