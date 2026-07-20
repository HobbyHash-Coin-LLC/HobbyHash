ifeq ($(HOST),armv7a-linux-android)
android_CXX=$(ANDROID_TOOLCHAIN_BIN)/$(HOST)eabi$(ANDROID_API_LEVEL)-clang++
android_CC=$(ANDROID_TOOLCHAIN_BIN)/$(HOST)eabi$(ANDROID_API_LEVEL)-clang
else
android_CXX=$(ANDROID_TOOLCHAIN_BIN)/$(HOST)$(ANDROID_API_LEVEL)-clang++
android_CC=$(ANDROID_TOOLCHAIN_BIN)/$(HOST)$(ANDROID_API_LEVEL)-clang
endif

android_AR=$(ANDROID_TOOLCHAIN_BIN)/llvm-ar
android_RANLIB=$(ANDROID_TOOLCHAIN_BIN)/llvm-ranlib
android_NM=$(ANDROID_TOOLCHAIN_BIN)/llvm-nm
android_STRIP=$(ANDROID_TOOLCHAIN_BIN)/llvm-strip
android_OBJCOPY=$(ANDROID_TOOLCHAIN_BIN)/llvm-objcopy

android_CFLAGS=-std=$(C_STANDARD)
android_CXXFLAGS=-std=$(CXX_STANDARD)

android_cmake_system_name=Linux
# Use a generic Linux system name so CMake does not invoke Android-Determine.cmake,
# which overrides the NDK API-level clang wrappers from depends.
android_cmake_system_version=3.17.0
