// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using System.Runtime.InteropServices;

using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwCamera
	{
		private ModelView m_Modelview = null;
		private string m_CameraName = null;
		private Camera m_Camera = null;
		private Vec3 UpVector = null;
		private Vec3 DirVector = null;
		private Vec3 Position = null;
		private float AspectRatio = 0f;
		private float FocalDistance = 0f;
		private float SensorWidth = 0f;
		public bool IsValid { get; private set; } = false;

		public SwCamera(ModelView modelview, string cameraname)
		{
			m_Modelview = modelview;
			m_CameraName = cameraname ?? "View";
			IsValid = Setup();
		}

		public SwCamera(Camera camera, string cameraname)
		{
			m_Camera = camera;
			m_CameraName = cameraname ?? "Camera";
			IsValid = Setup();
		}

		private bool Setup()
		{
			Camera cam = null;
			if (m_Camera != null)
			{
				cam = m_Camera;
			}
			else
			{
				if (m_Modelview != null && m_Modelview.Camera != null)
				{
					cam = m_Modelview.Camera;
				}
			}

			if (cam == null && m_Modelview != null)
				return SetupFromModelView();
			else if (cam != null)
				return SetupFromViewCamera(cam);
			return false;
		}

		private bool SetupFromViewCamera(Camera camera)
		{
			double[] data = camera.GetUpVector().ArrayData;
			UpVector = new Vec3(
				data[0],
				-data[2],
				data[1]);

			data = camera.GetViewVector().ArrayData;
			DirVector = new Vec3(
				data[0],
				-data[2],
				data[1]);

			//set world position
			double X, Y, Z;
			X = Y = Y = 0.0;
			camera.GetPositionCartesian(out X, out Y, out Z);
			Position = new Vec3((float)(X * SwSingleton.GeometryScale), -(float)(Z * SwSingleton.GeometryScale), (float)(Y * SwSingleton.GeometryScale));

			AspectRatio = (float)camera.AspectRatio;

			double focalDistance = camera.GetFocalDistance();
			FocalDistance = (float)(focalDistance * SwSingleton.GeometryScale);

			//Why this does not work ?
			//double sensorwidth = camera.FieldOfViewHeight * camera.AspectRatio;
			double sensorwidth = SwSingleton.GeometryScale * 2.0 * System.Math.Tan(camera.FieldOfViewAngle * 0.5) * focalDistance;
			//We correct the sensor width with experimental linear regression function so that SW fov and UE fov matches
			SensorWidth = (float)(sensorwidth * 1.212690775 + 14.9809190986);

			return true;
		}

		private bool SetupFromModelView()
		{
			double scale = m_Modelview.Scale2;

			bool wasorthographic = false;
			if (m_Modelview.HasPerspective() == false)
			{
				wasorthographic = true;
				m_Modelview.AddPerspective();
			}

			if (m_Modelview.HasPerspective())
			{
				//m_datasmithFacadeActorCamera.SetFocusDistance()
				MathTransform orientationmatrix = m_Modelview.Orientation3;
				MathTransform orientationmatrixinverse = orientationmatrix.Inverse();
				dynamic worldmatrixdata = orientationmatrixinverse.ArrayData;

				MathTransform modelviewtransforminverse = m_Modelview.Transform.Inverse();
				double[] eyepointdata = m_Modelview.GetEyePoint();
				MathPoint screen_eye_position = MathUtil.CreatePoint(eyepointdata[0], eyepointdata[1], eyepointdata[2]);
				MathPoint screen_world_position = screen_eye_position.MultiplyTransform(modelviewtransforminverse);
				dynamic positionarray = screen_world_position.ArrayData;

				DirVector = new Vec3(
					-worldmatrixdata[6],
					worldmatrixdata[8],
					-worldmatrixdata[7]);

				UpVector = new Vec3(
					worldmatrixdata[3],
					-worldmatrixdata[5],
					worldmatrixdata[4]);

				Position = new Vec3(
					positionarray[0] * SwSingleton.GeometryScale,
					-positionarray[2] * SwSingleton.GeometryScale,
					positionarray[1] * SwSingleton.GeometryScale);

				dynamic transforminversdata = modelviewtransforminverse.ArrayData;
				double framewidth = m_Modelview.FrameWidth * scale * transforminversdata[12];
				double frameheight = m_Modelview.FrameHeight * scale * transforminversdata[12];
				double aspectratio = framewidth / frameheight;
				double viewplanedistance = m_Modelview.GetViewPlaneDistance();
				viewplanedistance = viewplanedistance * transforminversdata[12] * scale;

				AspectRatio = (float)aspectratio;
				FocalDistance = (float)(viewplanedistance * framewidth * aspectratio * SwSingleton.GeometryScale);
				SensorWidth = (float)(framewidth * SwSingleton.GeometryScale);
			}
			else
			{
				//Did not found a way to setup an ortho cam using FDatasmithFacadeActorCamera (4.26) ???
				return false;
			}
			if (wasorthographic == true)
				m_Modelview.RemovePerspective();
			return true;
		}

		public FDatasmithFacadeActorCamera ToDatasmith()
		{
			FDatasmithFacadeActorCamera datasmithFacadeActorCamera = new FDatasmithFacadeActorCamera(m_CameraName);
			if (datasmithFacadeActorCamera != null)
			{
				datasmithFacadeActorCamera.SetCameraRotation(
					DirVector.x,
					DirVector.y,
					DirVector.z,
					UpVector.x,
					UpVector.y,
					UpVector.z
				);

				datasmithFacadeActorCamera.SetCameraPosition(
					Position.x,
					Position.y,
					Position.z);

				datasmithFacadeActorCamera.SetAspectRatio(AspectRatio);
				datasmithFacadeActorCamera.SetFocalLength(FocalDistance);
				datasmithFacadeActorCamera.SetSensorWidth(SensorWidth);
			}
			return datasmithFacadeActorCamera;
		}

		public static bool AreSame(SwCamera cam1, SwCamera cam2)
		{
			if (cam1.UpVector != cam2.UpVector) return false;
			if (cam1.DirVector != cam2.DirVector) return false;
			if (cam1.Position != cam2.Position) return false;
			if (!Utility.IsSame(cam1.AspectRatio, cam2.AspectRatio)) return false;
			if (!Utility.IsSame(cam1.FocalDistance, cam2.FocalDistance)) return false;
			if (!Utility.IsSame(cam1.SensorWidth, cam2.SensorWidth)) return false;
			return true;
		}
	}
}
