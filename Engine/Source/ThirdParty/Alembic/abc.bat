set MSBUILDEMITSOLUTION=1

call HDF5VS.bat
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\HDF5\HDF5.sln /p:Configuration=Debug;Platform=x64
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\HDF5\INSTALL.vcxproj /t:Rebuild /p:Configuration=Debug;Platform=x64
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\HDF5\HDF5.sln /p:Configuration=Release;Platform=x64
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\HDF5\INSTALL.vcxproj /t:Rebuild /p:Configuration=Release;Platform=x64

call AlembicVS.bat
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\alembic\Alembic.sln /p:Configuration=Debug;Platform=x64
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\alembic\INSTALL.vcxproj /t:Rebuild /p:Configuration=Debug;Platform=x64
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\alembic\Alembic.sln /p:Configuration=Release;Platform=x64
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" %cd%\build\VS2015\alembic\INSTALL.vcxproj /t:Rebuild /p:Configuration=Release;Platform=x64
