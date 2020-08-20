// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using System;

namespace DatasmithRhino
{
	public class FDatasmithRhinoProgressManager
	{
		private const int ProgressGranularity = 1000;
		private const string ExportMainMessage = "Datasmith Export";


		private static FDatasmithRhinoProgressManager SingeletonInstance;
		/**
		 *  Singleton Instance accessor.
		 */
		public static FDatasmithRhinoProgressManager Instance
		{
			get 
			{
				if (SingeletonInstance == null)
				{
					SingeletonInstance = new FDatasmithRhinoProgressManager();
				}

				return SingeletonInstance;
			}
		}

		private FDatasmithRhinoProgressManager()
		{
			const int LowerLimit = 0;
			const int UpperLimit = ProgressGranularity;
			const bool bEmbedLabel = true;
			const bool bShowPercent = true;
			Rhino.UI.StatusBar.ShowProgressMeter(LowerLimit, UpperLimit, ExportMainMessage, bEmbedLabel, bShowPercent);
		}

		/**
		 *	Use this function to start a new progress frame going from the previous AbsoluteEndValue value (or 0)
		 *	to the given AbsoluteEndValue. This should be called to define a change at the Macro level.
		 *	
		 *	@param Label							Task description displayed in the progress bar.
		 *	@param ProgressFrameAbsoluteEndValue	Absolute progression value this task will reach upon completion.
		 */
		public void StartMainTaskProgress(string Label, float ProgressFrameAbsoluteEndValue)
		{
			const int LowerLimit = 0;
			const int UpperLimit = ProgressGranularity;
			const bool bEmbedLabel = true;
			const bool bShowPercent = true;
			string ExportLabel = string.Format("{0} : {1}", ExportMainMessage, Label);
			int ProgressValue = (int)(ProgressFrameAbsoluteEndValue * UpperLimit);

			Rhino.UI.StatusBar.HideProgressMeter();
			Rhino.UI.StatusBar.ShowProgressMeter(LowerLimit, UpperLimit, ExportLabel, bEmbedLabel, bShowPercent);
			Rhino.UI.StatusBar.UpdateProgressMeter(ProgressFrameEnd, true);
			ProgressFrameStart = ProgressFrameEnd;
			ProgressFrameEnd = ProgressValue;
		}

		/**
		 * Use this function to update the current MainTask defined in StartMainTaskProgress().
		 * 
		 * @param PercentProgressFrame	Value between 0 and 1 defining the relative progress of the current "MainTask".
		 */
		public void UpdateCurrentTaskProgress(float PercentProgressFrame)
		{
			int ProgressDelta = (int)((ProgressFrameEnd - ProgressFrameStart) * PercentProgressFrame);
			Rhino.UI.StatusBar.UpdateProgressMeter(ProgressFrameStart + ProgressDelta, true);
		}

		public void StopProgress()
		{
			Rhino.UI.StatusBar.HideProgressMeter();
			ProgressFrameStart = 0;
			ProgressFrameEnd = 0;
		}

		private int ProgressFrameStart = 0;
		private int ProgressFrameEnd = 0;
	}
}