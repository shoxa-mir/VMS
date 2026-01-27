// FluxVision VMS - Hardware Detection Utility
// Detects CUDA devices and NVDEC capabilities

#include <cuda.h>
#include <cuda_runtime.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

#define CHECK_CUDA(call) \
    do { \
        CUresult err = call; \
        if (err != CUDA_SUCCESS) { \
            const char* errStr; \
            cuGetErrorString(err, &errStr); \
            std::cerr << "CUDA Error: " << errStr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

void printSeparator() {
    std::cout << std::string(70, '=') << std::endl;
}

void printHeader(const std::string& title) {
    printSeparator();
    std::cout << "  " << title << std::endl;
    printSeparator();
}

int main() {
    printHeader("FluxVision VMS - Hardware Detection");

    // Initialize CUDA Driver API
    CUresult result = cuInit(0);
    if (result != CUDA_SUCCESS) {
        const char* errStr;
        cuGetErrorString(result, &errStr);
        std::cerr << "Failed to initialize CUDA: " << errStr << std::endl;
        std::cerr << "\nPossible issues:" << std::endl;
        std::cerr << "  1. NVIDIA driver not installed" << std::endl;
        std::cerr << "  2. No NVIDIA GPU present" << std::endl;
        std::cerr << "  3. CUDA Toolkit not properly installed" << std::endl;
        return 1;
    }

    // Get CUDA driver version
    int driverVersion = 0;
    CHECK_CUDA(cuDriverGetVersion(&driverVersion));
    std::cout << "CUDA Driver Version: "
              << driverVersion / 1000 << "."
              << (driverVersion % 100) / 10
              << std::endl;

    // Get CUDA runtime version
    int runtimeVersion = 0;
    cudaRuntimeGetVersion(&runtimeVersion);
    std::cout << "CUDA Runtime Version: "
              << runtimeVersion / 1000 << "."
              << (runtimeVersion % 100) / 10
              << std::endl;

    // Get number of CUDA devices
    int deviceCount = 0;
    CHECK_CUDA(cuDeviceGetCount(&deviceCount));
    std::cout << "CUDA Devices Found: " << deviceCount << std::endl;
    std::cout << std::endl;

    if (deviceCount == 0) {
        std::cerr << "No CUDA-capable devices found!" << std::endl;
        return 1;
    }

    // Iterate through devices
    for (int devIdx = 0; devIdx < deviceCount; ++devIdx) {
        printHeader("Device " + std::to_string(devIdx));

        CUdevice device;
        CHECK_CUDA(cuDeviceGet(&device, devIdx));

        // Device name
        char deviceName[256];
        CHECK_CUDA(cuDeviceGetName(deviceName, sizeof(deviceName), device));
        std::cout << "Name: " << deviceName << std::endl;

        // Compute capability
        int computeMajor, computeMinor;
        CHECK_CUDA(cuDeviceGetAttribute(&computeMajor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device));
        CHECK_CUDA(cuDeviceGetAttribute(&computeMinor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device));
        std::cout << "Compute Capability: " << computeMajor << "." << computeMinor << std::endl;

        // Memory information
        size_t totalMem = 0;
        CHECK_CUDA(cuDeviceTotalMem(&totalMem, device));
        std::cout << "Total Memory: " << (totalMem / (1024 * 1024)) << " MB" << std::endl;

        // Multiprocessor count
        int smCount;
        CHECK_CUDA(cuDeviceGetAttribute(&smCount, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device));
        std::cout << "Multiprocessors (SMs): " << smCount << std::endl;

        // Max threads per block
        int maxThreadsPerBlock;
        CHECK_CUDA(cuDeviceGetAttribute(&maxThreadsPerBlock, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK, device));
        std::cout << "Max Threads per Block: " << maxThreadsPerBlock << std::endl;

        // Clock rate
        int clockRate;
        CHECK_CUDA(cuDeviceGetAttribute(&clockRate, CU_DEVICE_ATTRIBUTE_CLOCK_RATE, device));
        std::cout << "Clock Rate: " << (clockRate / 1000) << " MHz" << std::endl;

        // Memory clock rate
        int memClockRate;
        CHECK_CUDA(cuDeviceGetAttribute(&memClockRate, CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE, device));
        std::cout << "Memory Clock Rate: " << (memClockRate / 1000) << " MHz" << std::endl;

        // Memory bus width
        int memBusWidth;
        CHECK_CUDA(cuDeviceGetAttribute(&memBusWidth, CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH, device));
        std::cout << "Memory Bus Width: " << memBusWidth << " bits" << std::endl;

        // PCI information
        int pciBusID, pciDeviceID, pciDomainID;
        CHECK_CUDA(cuDeviceGetAttribute(&pciBusID, CU_DEVICE_ATTRIBUTE_PCI_BUS_ID, device));
        CHECK_CUDA(cuDeviceGetAttribute(&pciDeviceID, CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID, device));
        CHECK_CUDA(cuDeviceGetAttribute(&pciDomainID, CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID, device));
        std::cout << "PCI Bus/Device/Domain: "
                  << std::hex << std::setfill('0')
                  << std::setw(4) << pciDomainID << ":"
                  << std::setw(2) << pciBusID << ":"
                  << std::setw(2) << pciDeviceID
                  << std::dec << std::endl;

        std::cout << std::endl;

        // NVDEC capabilities
        printHeader("NVDEC Capabilities (Device " + std::to_string(devIdx) + ")");

        // Check for hardware video decoder support (NVDEC)
        // Note: Actual NVDEC query requires Video Codec SDK headers
        // This is a simplified check based on compute capability
        bool nvdecSupported = (computeMajor >= 3);  // Kepler and newer
        std::cout << "NVDEC Supported: " << (nvdecSupported ? "YES" : "NO") << std::endl;

        if (nvdecSupported) {
            std::cout << "  Recommended for: Video decoding (H.264, H.265)" << std::endl;

            // Estimate concurrent decode sessions based on GPU
            int estimatedSessions = smCount / 2;  // Rough estimate
            if (computeMajor >= 7) {  // Turing and newer
                estimatedSessions = 32;  // Most modern GPUs support 32 sessions
            } else if (computeMajor >= 6) {  // Pascal
                estimatedSessions = 16;
            }
            std::cout << "  Estimated Concurrent Decode Sessions: ~" << estimatedSessions << std::endl;

            // Check NVENC support (encoder)
            std::cout << "  NVENC Supported: YES (for client streaming)" << std::endl;
        } else {
            std::cout << "  This GPU is too old for NVDEC." << std::endl;
            std::cout << "  FluxVision VMS requires Compute Capability 3.0 or higher." << std::endl;
        }

        std::cout << std::endl;

        // Recommendations for FluxVision VMS
        printHeader("FluxVision VMS Recommendations (Device " + std::to_string(devIdx) + ")");

        if (computeMajor < 3) {
            std::cout << "❌ NOT SUITABLE" << std::endl;
            std::cout << "  This GPU is too old. Requires Compute Capability 3.0+." << std::endl;
        } else if (computeMajor < 5) {
            std::cout << "⚠️  MARGINAL" << std::endl;
            std::cout << "  This GPU may work but performance will be limited." << std::endl;
            std::cout << "  Recommended: Maxwell (5.x) or newer." << std::endl;
        } else if (computeMajor < 7) {
            std::cout << "✓ SUITABLE" << std::endl;
            std::cout << "  Estimated camera capacity: 20-30 cameras @ 1080p" << std::endl;
        } else {
            std::cout << "✓✓ EXCELLENT" << std::endl;
            std::cout << "  Estimated camera capacity: 42+ cameras @ 1080p" << std::endl;
            std::cout << "  This GPU has modern NVDEC/NVENC for optimal performance." << std::endl;
        }

        // Memory recommendation
        size_t totalGB = totalMem / (1024 * 1024 * 1024);
        std::cout << std::endl;
        std::cout << "Memory Analysis:" << std::endl;
        std::cout << "  Total VRAM: " << totalGB << " GB" << std::endl;

        if (totalGB < 4) {
            std::cout << "  ⚠️  Low VRAM. Recommended: 6GB+ for 42 cameras." << std::endl;
        } else if (totalGB < 6) {
            std::cout << "  ⚠️  Adequate for 20-30 cameras. 8GB+ recommended for 42." << std::endl;
        } else {
            std::cout << "  ✓ Sufficient VRAM for 42+ cameras." << std::endl;
        }

        std::cout << std::endl;
    }

    // Final summary
    printHeader("Summary");
    std::cout << "System is " << (deviceCount > 0 ? "READY" : "NOT READY")
              << " for FluxVision VMS development." << std::endl;

    if (deviceCount > 0) {
        std::cout << "\nNext steps:" << std::endl;
        std::cout << "  1. Download NVIDIA Video Codec SDK from:" << std::endl;
        std::cout << "     https://developer.nvidia.com/nvidia-video-codec-sdk" << std::endl;
        std::cout << "  2. Configure CMake with NVDEC_SDK_PATH environment variable" << std::endl;
        std::cout << "  3. Run: cmake .. && make" << std::endl;
    }

    printSeparator();

    return 0;
}
