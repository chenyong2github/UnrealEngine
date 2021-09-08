// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidworksDatasmith.SwObjects;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith
{
	[ComVisible(false)]
	public static class SwSingleton
	{
		private static SwNotifications _notifications = new SwNotifications();
		public static SwNotifications Events { get { return _notifications; } }

		private static Dictionary<string, SwScene> _scenes = new Dictionary<string, SwScene>();
		private static SwScene _currentScene = null;
		public static SwScene CurrentScene { get { return _currentScene; } }

		public static float GeometryScale = 1f;

		public class ProgressArgs : EventArgs
		{
			public ProgressArgs(string message)
			{
				Message = message;
			}
			public string Message { get; private set; }
		}

		static public event EventHandler<ProgressArgs> ProgressEvent;

		public static void SwitchToScene(ModelDoc2 doc)
		{
			var name = doc.GetPathName();

			// the units setup seems to only influence the display of measurements
			// the vertices returned by solidworks are based on metres
			// because unreal is based on centimetres, we simply need to multiply distances by 100

			GeometryScale = 100f;

			/*
			int units = doc.Extension.GetUserPreferenceInteger((int)swUserPreferenceIntegerValue_e.swUnitsLinear, (int)swUserPreferenceOption_e.swDetailingNoOptionSpecified);
			if (units == (int)swLengthUnit_e.swANGSTROM) GeometryScale = 1f/100000000f;
			else if (units == (int)swLengthUnit_e.swCM) GeometryScale = 1f;
			else if (units == (int)swLengthUnit_e.swFEET) GeometryScale = 30.48f;
			else if (units == (int)swLengthUnit_e.swFEETINCHES) GeometryScale = 1f;
			else if (units == (int)swLengthUnit_e.swINCHES) GeometryScale = 2.54f;
			else if (units == (int)swLengthUnit_e.swMETER) GeometryScale = 100f;
			else if (units == (int)swLengthUnit_e.swMICRON) GeometryScale = 1f/10000f;
			else if (units == (int)swLengthUnit_e.swMIL) GeometryScale = 160934.4f;
			else if (units == (int)swLengthUnit_e.swMM) GeometryScale = 1f/10f;
			else if (units == (int)swLengthUnit_e.swNANOMETER) GeometryScale = 1f/10000000f;
			else if (units == (int)swLengthUnit_e.swUIN) GeometryScale = 0.00000254f;
			*/

			if (_scenes.ContainsKey(name))
				_currentScene = _scenes[name];
			else
			{
				_currentScene = new SwScene(doc);
				_scenes.Add(name, _currentScene);
			}
		}
		
		public static void SynchScenesOpened(List<string> opendocumentpaths)
		{
			List<string> scenestoremove = new List<string>();
			foreach (var item in _scenes)
			{
				if (opendocumentpaths.Contains(item.Key) == false)
				{
					scenestoremove.Add(item.Key);
				}
			}

			foreach (var item in scenestoremove)
			{
				SwScene scene = _scenes[item];
				if (scene == _currentScene)
				{
					_currentScene = null;
				}
				scene.Processor.Stop();
				_scenes.Remove(item);
			}

			if (_currentScene == null && _scenes.Count > 0)
			{
				SwitchToScene(_scenes.First().Value.Doc);
			}
		}

		public static void FireProgressEvent(string msg)
		{
			EventHandler<ProgressArgs> handler = ProgressEvent;
			handler?.Invoke(null, new ProgressArgs(msg));
		}

		public static void FireStopProgressEvent()
		{
			EventHandler<ProgressArgs> handler = ProgressEvent;
			handler?.Invoke(null, new ProgressArgs(null));
		}
	}
}
