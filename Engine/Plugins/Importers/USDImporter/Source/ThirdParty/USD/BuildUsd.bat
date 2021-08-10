set OUT_DIR=%CD%\vs2019
mkdir %OUT_DIR%

python src\build_scripts\build_usd.py "%OUT_DIR%" --no-tests --no-examples --no-tutorials --no-tools --no-docs --no-imaging --generator "Visual Studio 16 2019"

pause
