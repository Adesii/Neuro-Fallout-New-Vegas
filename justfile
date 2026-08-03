debug: 
  msvc-x86-cmake --build build 
rebuild:
  rm -rf build
  msvc-x86-cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/opt/msvc/bin/x86/cl 
  msvc-x86-cmake --build build

rebuild-debug:
  rm -rf build
  msvc-x86-cmake -B build -DDEBUG=true -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/opt/msvc/bin/x86/cl 
  msvc-x86-cmake --build build
