set OUT_DIR=%CD%\vs2017
mkdir %OUT_DIR%

python src\build_scripts\build_usd.py "%OUT_DIR%" --no-docs --no-tests --no-tools --no-imaging --generator "Visual Studio 15 2017 Win64"

pause