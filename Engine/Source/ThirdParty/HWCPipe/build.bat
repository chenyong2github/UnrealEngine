set CMAKE_BIN=%ANDROID_HOME%\cmake\3.10.2.4988404\bin

REM Build ARMv7
%CMAKE_BIN%\cmake.exe -DANDROID_ABI=armeabi-v7a -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=%CMAKE_BIN%\ninja.exe -DANDROID_STL=c++_shared -G Ninja -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK_ROOT%\build\cmake\android.toolchain.cmake -DANDROID_NATIVE_API_LEVEL=23 -H%CD%\include\ -Bbuild\armeabi-v7a\

%CMAKE_BIN%\ninja.exe -C build\armeabi-v7a\

copy build\armeabi-v7a\libhwcpipe.so lib\armeabi-v7a\libhwcpipe.so

REM Build ARMv8
%CMAKE_BIN%\cmake.exe -DANDROID_ABI=arm64-v8a -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=%CMAKE_BIN%\ninja.exe -DANDROID_STL=c++_shared -G Ninja -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK_ROOT%\build\cmake\android.toolchain.cmake -DANDROID_NATIVE_API_LEVEL=23 -H%CD%\include\ -Bbuild\arm64-v8a\

%CMAKE_BIN%\ninja.exe -C build\arm64-v8a\

copy build\arm64-v8a\libhwcpipe.so lib\arm64-v8a\libhwcpipe.so