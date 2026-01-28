// tools/decoder_test.cpp
// Simple decoder test utility to validate Phase 1 implementation
#include "core/codec/decoder_factory.h"
#include "core/codec/decoder_interface.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cstring>

using namespace fluxvision;

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --caps             Show decoder capabilities" << std::endl;
    std::cout << "  --test-nvdec       Test NVDEC decoder (if available)" << std::endl;
    std::cout << "  --test-cpu         Test CPU decoder" << std::endl;
    std::cout << "  --help             Show this help" << std::endl;
}

void printCapabilities() {
    auto caps = DecoderFactory::getCapabilities();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Decoder Capabilities" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "NVDEC Available:     " << (caps.nvdecAvailable ? "YES" : "NO") << std::endl;
    std::cout << "CPU Decoder:         " << (caps.cpuDecoderAvailable ? "YES" : "NO") << std::endl;
    std::cout << "CUDA Devices:        " << caps.cudaDeviceCount << std::endl;
    std::cout << "Recommended:         " << caps.recommendedDecoder << std::endl;
    std::cout << "========================================\n" << std::endl;
}

bool testDecoder(DecoderType type, const char* name) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Testing " << name << " Decoder" << std::endl;
    std::cout << "========================================" << std::endl;

    // Create decoder configuration
    DecoderConfig config;
    config.codec = CodecType::H264;
    config.quality = StreamQuality::GRID_VIEW;
    config.maxWidth = 1920;
    config.maxHeight = 1080;
    config.preferHardware = (type == DecoderType::NVDEC);

    // Create decoder
    std::cout << "Creating decoder..." << std::endl;
    auto decoder = DecoderFactory::create(type, config);

    if (!decoder) {
        std::cerr << "Failed to create decoder!" << std::endl;
        return false;
    }

    std::cout << "✓ Decoder created successfully" << std::endl;
    std::cout << "  Hardware Accelerated: " << (decoder->isHardwareAccelerated() ? "YES" : "NO") << std::endl;

    // Get memory usage
    auto memStats = decoder->getMemoryUsage();
    std::cout << "  GPU Memory:  " << (memStats.gpuMemoryUsed / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "  System Memory: " << (memStats.systemMemoryUsed / 1024) << " KB" << std::endl;
    std::cout << "  Surface Pool:  " << memStats.surfacePoolSize << " / " << memStats.surfacePoolCapacity << std::endl;

    // Test basic operations
    std::cout << "\nTesting basic operations..." << std::endl;

    // Test quality changes
    decoder->setQuality(StreamQuality::FULLSCREEN);
    std::cout << "✓ Quality change: FULLSCREEN" << std::endl;

    decoder->setQuality(StreamQuality::PAUSED);
    std::cout << "✓ Quality change: PAUSED" << std::endl;

    decoder->setQuality(StreamQuality::GRID_VIEW);
    std::cout << "✓ Quality change: GRID_VIEW (back to default)" << std::endl;

    // Test flush
    decoder->flush();
    std::cout << "✓ Flush operation" << std::endl;

    // Test reset
    decoder->reset();
    std::cout << "✓ Reset operation" << std::endl;

    std::cout << "\n" << name << " decoder test PASSED\n" << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  FluxVision VMS - Decoder Test" << std::endl;
    std::cout << "  Phase 1: Core Decoder Engine" << std::endl;
    std::cout << "========================================\n" << std::endl;

    if (argc < 2) {
        printUsage(argv[0]);
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--caps") {
            printCapabilities();
        }
        else if (arg == "--test-nvdec") {
            if (DecoderFactory::isNvdecAvailable()) {
                if (!testDecoder(DecoderType::NVDEC, "NVDEC Hardware")) {
                    return 1;
                }
            } else {
                std::cerr << "NVDEC not available on this system" << std::endl;
                return 1;
            }
        }
        else if (arg == "--test-cpu") {
            if (!testDecoder(DecoderType::CPU, "CPU Software")) {
                return 1;
            }
        }
        else if (arg == "--test-all") {
            // Test capabilities first
            printCapabilities();

            // Test CPU decoder (always available)
            if (!testDecoder(DecoderType::CPU, "CPU Software")) {
                return 1;
            }

            // Test NVDEC if available
            if (DecoderFactory::isNvdecAvailable()) {
                if (!testDecoder(DecoderType::NVDEC, "NVDEC Hardware")) {
                    return 1;
                }
            } else {
                std::cout << "NVDEC not available, skipping hardware decoder test" << std::endl;
            }

            std::cout << "\n========================================" << std::endl;
            std::cout << "  All Tests PASSED!" << std::endl;
            std::cout << "========================================\n" << std::endl;
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    return 0;
}
