/**
 * Network Layer Test Utility
 * Phase 2: Network Layer Validation
 *
 * Tests:
 * - RTSP connection to camera
 * - H.264 bitstream reception (FFmpeg handles RTP)
 * - NAL unit extraction from bitstream
 * - H.264 SPS/PPS parsing
 * - Basic network statistics
 */

#include "core/network/rtsp_client.h"
#include "core/network/h264_parser.h"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>

using namespace fluxvision::network;

struct TestConfig {
    std::string rtspUrl;
    std::string username;
    std::string password;
    int durationSeconds = 10;
    bool verbose = false;
};

void printUsage(const char* programName) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  FluxVision VMS - Network Test" << std::endl;
    std::cout << "  Phase 2: Network Layer" << std::endl;
    std::cout << "========================================\n" << std::endl;

    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --url <rtsp://...>    RTSP URL (required)" << std::endl;
    std::cout << "  --user <username>     Username (optional)" << std::endl;
    std::cout << "  --pass <password>     Password (optional)" << std::endl;
    std::cout << "  --duration <seconds>  Test duration (default: 10)" << std::endl;
    std::cout << "  --verbose             Print detailed packet info" << std::endl;
    std::cout << "  --help                Show this help" << std::endl;

    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << programName << " --url rtsp://192.168.1.100:554/stream1" << std::endl;
    std::cout << "  " << programName << " --url rtsp://cam.example.com/stream --user admin --pass 12345" << std::endl;
}

bool parseArgs(int argc, char** argv, TestConfig& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--url" && i + 1 < argc) {
            config.rtspUrl = argv[++i];
        }
        else if (arg == "--user" && i + 1 < argc) {
            config.username = argv[++i];
        }
        else if (arg == "--pass" && i + 1 < argc) {
            config.password = argv[++i];
        }
        else if (arg == "--duration" && i + 1 < argc) {
            config.durationSeconds = std::stoi(argv[++i]);
        }
        else if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
        }
    }

    if (config.rtspUrl.empty()) {
        std::cerr << "Error: RTSP URL required (use --url)" << std::endl;
        printUsage(argv[0]);
        return false;
    }

    return true;
}

void printStats(const NetworkStats& netStats, int totalNalUnits) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Network Statistics" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "H.264 packets received: " << netStats.packetsReceived << std::endl;
    std::cout << "Total NAL units:        " << totalNalUnits << std::endl;
    std::cout << "Bytes received:         " << netStats.bytesReceived / 1024 << " KB" << std::endl;
    std::cout << "Bitrate:                " << std::fixed << std::setprecision(2)
              << netStats.bitrate << " Mbps" << std::endl;
    std::cout << "Uptime:                 " << netStats.uptime << " seconds" << std::endl;

    if (netStats.packetsReceived > 0) {
        double avgNalsPerPacket = static_cast<double>(totalNalUnits) / netStats.packetsReceived;
        std::cout << "Avg NALs/packet:        " << std::fixed << std::setprecision(1)
                  << avgNalsPerPacket << std::endl;
    }
}

int main(int argc, char** argv) {
    TestConfig config;

    if (!parseArgs(argc, argv, config)) {
        return 1;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Testing RTSP Connection" << std::endl;
    std::cout << "========================================\n" << std::endl;

    std::cout << "RTSP URL: " << config.rtspUrl << std::endl;
    std::cout << "Duration: " << config.durationSeconds << " seconds" << std::endl;

    // Create RTSP client
    RtspClient client;
    RtspClient::Config rtspConfig;
    rtspConfig.url = config.rtspUrl;
    rtspConfig.username = config.username;
    rtspConfig.password = config.password;
    rtspConfig.transport = TransportType::TCP;
    rtspConfig.lowLatency = true;

    std::cout << "\nConnecting..." << std::endl;
    if (!client.connect(rtspConfig)) {
        std::cerr << "✗ Failed to connect to RTSP stream" << std::endl;
        return 1;
    }

    std::cout << "✓ Connected successfully" << std::endl;

    // Get stream info
    int width = 0, height = 0, framerate = 0;
    if (client.getStreamInfo(width, height, framerate)) {
        std::cout << "  Resolution: " << width << "x" << height << std::endl;
        std::cout << "  Framerate:  " << framerate << " fps" << std::endl;
    }

    // Extract SPS/PPS from codec extradata (sent in RTSP SDP)
    std::vector<NalUnit> extraNals;
    if (client.getExtradata(extraNals)) {
        std::cout << "\n✓ Extradata found: " << extraNals.size() << " NAL units (SPS/PPS from RTSP SDP)" << std::endl;
        for (const auto& nal : extraNals) {
            if (nal.type == NalUnitType::SPS) {
                std::cout << "  - SPS (" << nal.data.size() << " bytes)" << std::endl;

                // Parse SPS for stream info
                SPSInfo sps;
                if (H264Parser::extractSPS(nal.data.data(), nal.data.size(), sps)) {
                    std::cout << "    Resolution: " << sps.width << "x" << sps.height << std::endl;
                    std::cout << "    Framerate:  " << sps.framerate << " fps" << std::endl;
                    std::cout << "    Profile:    " << sps.profile << std::endl;
                    std::cout << "    Level:      " << sps.level << std::endl;
                }
            } else if (nal.type == NalUnitType::PPS) {
                std::cout << "  - PPS (" << nal.data.size() << " bytes)" << std::endl;
            }
        }
    }

    // Receive NAL units directly from RTSP client
    // FFmpeg handles RTSP/RTP, we just parse H.264 bitstream
    std::cout << "\nReceiving NAL units..." << std::endl;

    auto startTime = std::chrono::steady_clock::now();
    int nalCount = 0;
    int spsCount = extraNals.size() > 0 ? 1 : 0;  // Count extradata SPS
    int ppsCount = extraNals.size() > 1 ? 1 : 0;  // Count extradata PPS
    int idrCount = 0;
    bool foundSPS = !extraNals.empty();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

        if (elapsed >= config.durationSeconds) {
            break;
        }

        // Receive NAL units (FFmpeg already handles RTP depacketization)
        std::vector<NalUnit> nalUnits;
        int received = client.receiveNalUnits(nalUnits);

        if (received == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Process NAL units
        for (auto& nal : nalUnits) {
            nalCount++;

            if (config.verbose) {
                std::cout << "NAL #" << nalCount << " - Type: "
                          << static_cast<int>(nal.type)
                          << " Size: " << nal.data.size() << " bytes"
                          << " Keyframe: " << (nal.isKeyframe ? "YES" : "NO")
                          << std::endl;
            }

            // Count NAL types
            if (nal.type == NalUnitType::SPS) {
                spsCount++;

                // Parse SPS if we haven't yet
                if (!foundSPS) {
                    SPSInfo sps;
                    if (H264Parser::extractSPS(nal.data.data(), nal.data.size(), sps)) {
                        std::cout << "\n✓ SPS Parsed:" << std::endl;
                        std::cout << "  Resolution: " << sps.width << "x" << sps.height << std::endl;
                        std::cout << "  Framerate:  " << sps.framerate << " fps" << std::endl;
                        std::cout << "  Profile:    " << sps.profile << std::endl;
                        std::cout << "  Level:      " << sps.level << std::endl;
                        std::cout << "  Interlaced: " << (sps.interlaced ? "YES" : "NO") << std::endl;
                        foundSPS = true;
                    }
                }
            }
            else if (nal.type == NalUnitType::PPS) {
                ppsCount++;
            }
            else if (nal.type == NalUnitType::IDR) {
                idrCount++;
            }

            // Print progress every 100 NAL units
            if (!config.verbose && nalCount % 100 == 0) {
                std::cout << "  Received " << nalCount << " NAL units..." << std::endl;
            }
        }
    }

    std::cout << "\nDisconnecting..." << std::endl;
    client.disconnect();

    // Print final statistics
    std::cout << "\n========================================" << std::endl;
    std::cout << "  NAL Unit Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total NAL units:  " << nalCount << std::endl;
    std::cout << "SPS (headers):    " << spsCount << std::endl;
    std::cout << "PPS (headers):    " << ppsCount << std::endl;
    std::cout << "IDR (keyframes):  " << idrCount << std::endl;

    NetworkStats netStats = client.getStats();
    printStats(netStats, nalCount);

    // Test result
    std::cout << "\n========================================" << std::endl;
    if (nalCount > 0 && foundSPS) {
        std::cout << "  Network test PASSED" << std::endl;
    } else {
        std::cout << "  Network test FAILED" << std::endl;
        std::cout << "  (NAL units: " << nalCount << ", SPS found: " << foundSPS << ")" << std::endl;
    }
    std::cout << "========================================\n" << std::endl;

    return (nalCount > 0 && foundSPS) ? 0 : 1;
}
