Write-Host "Configuring CMake for STM32H7 cross-compilation..."
cmake -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=mingw32-make "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DMCU_FAMILY=STM32H7"

if ($LASTEXITCODE -eq 0) {
    Write-Host "Building the project..."
    cmake --build build
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Build Successful! You can find the binary at: build/ports/stm32/h7/stm32h7_blinky.bin"
    } else {
        Write-Host "Build failed."
    }
} else {
    Write-Host "CMake configuration failed."
}
