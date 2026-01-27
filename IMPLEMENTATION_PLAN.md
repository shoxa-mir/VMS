# FluxVision VMS v2 - Implementation Plan

## Executive Summary

Building a professional-grade VMS from scratch, inspired by NX Witness architecture, capable of handling **42+ cameras** with **50%+ resource headroom** for AI/analytics.

**Core Problem with v1**: GStreamer's per-camera pipeline model creates 100+ threads and performs unnecessary decode→encode→decode cycles, consuming all system resources.

**Solution**: Direct hardware decoding with zero-copy GPU rendering, fixed thread pools, and adaptive quality streaming.

---

## Target Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     FluxVision VMS v2                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │   Camera 1   │    │   Camera 2   │    │   Camera N...    │  │
│  │  RTSP/TCP    │    │  RTSP/TCP    │    │   RTSP/TCP       │  │
│  └──────┬───────┘    └──────┬───────┘    └────────┬─────────┘  │
│         │                   │                     │             │
│         ▼                   ▼                     ▼             │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              Network Thread Pool (8 threads)            │   │
│  │         RTSP Connection + RTP Packet Reception          │   │
│  └─────────────────────────┬───────────────────────────────┘   │
│                            │                                    │
│                            ▼                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │           Lock-Free Packet Queue (per camera)           │   │
│  └─────────────────────────┬───────────────────────────────┘   │
│                            │                                    │
│                            ▼                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │             Decode Thread Pool (4-8 threads)            │   │
│  │                                                         │   │
│  │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │   │
│  │   │   NVDEC     │  │   NVDEC     │  │   NVDEC     │    │   │
│  │   │  Session 1  │  │  Session 2  │  │  Session N  │    │   │
│  │   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘    │   │
│  │          │                │                │           │   │
│  │          └────────────────┼────────────────┘           │   │
│  │                           │                            │   │
│  └───────────────────────────┼────────────────────────────┘   │
│                              │                                 │
│                              ▼                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              GPU Memory (CUDA Surfaces)                 │   │
│  │         Decoded frames stay in GPU memory               │   │
│  └─────────────────────────┬───────────────────────────────┘   │
│                            │                                    │
│                            ▼                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │           CUDA-OpenGL Interop (Zero-Copy)               │   │
│  │        Map CUDA surfaces directly to GL textures        │   │
│  └─────────────────────────┬───────────────────────────────┘   │
│                            │                                    │
│                            ▼                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              OpenGL Render Thread (1 thread)            │   │
│  │                                                         │   │
│  │   ┌─────────────────────────────────────────────────┐  │   │
│  │   │            Texture Atlas (All Cameras)          │  │   │
│  │   │     Single draw call renders entire grid        │  │   │
│  │   └─────────────────────────────────────────────────┘  │   │
│  │                                                         │   │
│  └─────────────────────────┬───────────────────────────────┘   │
│                            │                                    │
│                            ▼                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Qt5 UI Layer                         │   │
│  │         QOpenGLWidget with custom rendering             │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│                    Parallel: Recording Engine                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │   H264 Bitstream → File Writer (no re-encoding!)        │   │
│  │   Direct passthrough from RTP packets to MKV            │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘

Total Threads: ~15-20 (vs 100+ in v1)
CPU Usage Target: <25% for 42 cameras
GPU Usage Target: <40% for 42 cameras
Headroom: 50%+ for AI/analytics
```

---

## Performance Targets

| Metric | v1 (Current) | v2 (Target) | Improvement |
|--------|--------------|-------------|-------------|
| Cameras | 25-30 max | 42+ smooth | 1.5x |
| CPU Usage | 40-50% | <20% | 2.5x |
| RAM Usage | 1.85GB | <800MB | 2.3x |
| VRAM Usage | 7-8GB | <4GB | 2x |
| Thread Count | 100+ | ~20 | 5x |
| Latency | 300-400ms | <100ms | 4x |
| AI Headroom | 0% | 50%+ | ∞ |

---

## KCMVP Encryption Architecture

### Overview

All sensitive data encrypted using KCMVP (Korea Cryptographic Module Validation Program) certified algorithms. This ensures compliance with Korean government security requirements.

### Algorithm Selection

| Data Type | Algorithm | Mode | Key Size | Rationale |
|-----------|-----------|------|----------|-----------|
| **Video Recordings** | LEA | GCM | 256-bit | 1.5-2x faster than AES, optimized for high-throughput streaming |
| **User Credentials** | ARIA | GCM | 256-bit | Korean standard block cipher, mandatory for KCMVP |
| **Config/Settings** | ARIA | GCM | 256-bit | Authenticated encryption for tamper detection |
| **Database Fields** | ARIA | CBC + HMAC | 256-bit | Encrypt-then-MAC for SQLite field-level encryption |
| **Session Tokens** | ARIA | CTR | 256-bit | Fast counter mode for short-lived tokens |
| **Key Derivation** | PBKDF2 | SHA-256 | - | 100,000 iterations minimum |
| **Key Exchange** | ECDH | P-256 | 256-bit | Client-server session key agreement |
| **Integrity Check** | HMAC | SHA-256 | 256-bit | Segment tampering detection |
| **Digital Signature** | ECDSA | P-256 | 256-bit | Audit log signing |

### Encryption Data Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        KCMVP Encryption Layer                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  VIDEO RECORDING PATH (High-Throughput)                                 │
│  ─────────────────────────────────────                                  │
│  H264 NAL Unit                                                          │
│      │                                                                  │
│      ▼                                                                  │
│  ┌────────────────────────────────────────┐                            │
│  │     LEA-256-GCM Encryption            │                            │
│  │  • 12-byte random IV per segment       │                            │
│  │  • AAD: camera_id + timestamp          │                            │
│  │  • 16-byte authentication tag          │                            │
│  └────────────────────────────────────────┘                            │
│      │                                                                  │
│      ▼                                                                  │
│  Encrypted MKV Segment (.mkv.enc)                                       │
│                                                                         │
│  ───────────────────────────────────────────────────────────────────    │
│                                                                         │
│  USER DATA PATH                                                         │
│  ──────────────                                                         │
│  User Password                                                          │
│      │                                                                  │
│      ▼                                                                  │
│  ┌────────────────────────────────────────┐                            │
│  │     PBKDF2-SHA-256 (100K iterations)   │                            │
│  │  • 32-byte random salt per user        │                            │
│  │  • Output: 256-bit derived key         │                            │
│  └────────────────────────────────────────┘                            │
│      │                                                                  │
│      ▼                                                                  │
│  Master Key (encrypted with derived key)                                │
│      │                                                                  │
│      ▼                                                                  │
│  ┌────────────────────────────────────────┐                            │
│  │     ARIA-256-GCM                       │                            │
│  │  • Encrypts: credentials, API keys     │                            │
│  │  • Stored in secure keychain           │                            │
│  └────────────────────────────────────────┘                            │
│                                                                         │
│  ───────────────────────────────────────────────────────────────────    │
│                                                                         │
│  CLIENT-SERVER KEY EXCHANGE                                             │
│  ─────────────────────────────                                          │
│  ┌──────────┐              ┌──────────┐                                │
│  │  Client  │              │  Server  │                                │
│  └────┬─────┘              └────┬─────┘                                │
│       │  ECDH P-256 KeyGen      │                                      │
│       │  ───────────────────►   │                                      │
│       │    (Client Public Key)  │                                      │
│       │                         │                                      │
│       │   ◄───────────────────  │                                      │
│       │    (Server Public Key)  │                                      │
│       │                         │                                      │
│       ▼                         ▼                                      │
│  Shared Secret (256-bit)   Shared Secret                               │
│       │                         │                                      │
│       ▼                         ▼                                      │
│  Session Key (HKDF)        Session Key                                 │
│                                                                         │
│  ───────────────────────────────────────────────────────────────────    │
│                                                                         │
│  DATABASE ENCRYPTION                                                    │
│  ───────────────────                                                    │
│  SQLite Field                                                           │
│      │                                                                  │
│      ▼                                                                  │
│  ┌────────────────────────────────────────┐                            │
│  │     ARIA-256-CBC + HMAC-SHA-256       │                            │
│  │  • Encrypt-then-MAC pattern            │                            │
│  │  • Per-field random IV                 │                            │
│  │  • HMAC covers IV + ciphertext         │                            │
│  └────────────────────────────────────────┘                            │
│      │                                                                  │
│      ▼                                                                  │
│  Encrypted Field (Base64: IV || Ciphertext || MAC)                     │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Hierarchy

```
┌─────────────────────────────────────────────────────────────────┐
│                      Key Management Hierarchy                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Level 0: Root Key (Hardware-backed if available)               │
│  ─────────────────────────────────────────────                  │
│  • Generated once during system initialization                   │
│  • Stored in TPM/HSM if available, else encrypted file          │
│  • Never leaves secure storage                                   │
│      │                                                           │
│      ├──► Level 1: Master Encryption Key (MEK)                  │
│      │    • Encrypts all Data Encryption Keys                    │
│      │    • Rotated annually or on-demand                        │
│      │        │                                                  │
│      │        ├──► Level 2: Video DEK (per camera)              │
│      │        │    • LEA-256-GCM key                            │
│      │        │    • Rotated daily at midnight                   │
│      │        │                                                  │
│      │        ├──► Level 2: Database DEK                        │
│      │        │    • ARIA-256 key for SQLite                    │
│      │        │    • Rotated monthly                             │
│      │        │                                                  │
│      │        └──► Level 2: Config DEK                          │
│      │             • ARIA-256 key for settings                   │
│      │             • Rotated on admin password change            │
│      │                                                           │
│      └──► Level 1: User Key Encryption Key (UKEK)               │
│           • Derived from user password via PBKDF2                │
│           • Encrypts user-specific secrets                       │
│               │                                                  │
│               └──► Level 2: User Session Key                    │
│                    • ECDH-derived per session                    │
│                    • Expires after 24 hours                      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Encrypted File Format

#### Video Segment (Extension-less or Custom)
```
Filename: 20260126/1738012340  (timestamp only, no extension)
OR: 20260126/1738012340.fvd    (FluxVision Data - obfuscated)
OR: 20260126/a3f5c8e2b9d1...   (SHA256 hash first 32 chars)

┌────────────────────────────────────────────────────────────┐
│ Byte Offset │ Field              │ Size      │ Description │
├─────────────┼────────────────────┼───────────┼─────────────┤
│ 0           │ Magic              │ 4 bytes   │ "FVMS"      │
│ 4           │ Version            │ 2 bytes   │ 0x0001      │
│ 6           │ Algorithm          │ 1 byte    │ 0x01 = LEA  │
│ 7           │ Key ID             │ 16 bytes  │ UUID        │
│ 23          │ IV                 │ 12 bytes  │ Random      │
│ 35          │ Encrypted Data     │ Variable  │ LEA-GCM     │
│ -16         │ Auth Tag           │ 16 bytes  │ GCM Tag     │
└────────────────────────────────────────────────────────────┘

** Actual format (MKV) stored in database metadata, not filename **
```

#### Encrypted Database Field
```
Base64(IV[16] || Ciphertext[N] || HMAC[32])
```

---

### Decryption Flow (Playback & Export)

#### **Playback Decryption** (Real-time)

```cpp
// src/recording/encrypted_segment_reader.h
class EncryptedSegmentReader {
public:
    // Open encrypted segment for playback
    bool open(const std::string& filePath);

    // Read header to get Key ID
    bool readHeader(SegmentHeader& header);

    // Decrypt and get NAL units for playback
    bool readNalUnit(NalUnit& nal);

    // Close segment
    void close();

private:
    std::ifstream file_;
    SegmentHeader header_;
    std::vector<uint8_t> sessionKey_;  // Fetched from key manager using Key ID
    LEACipher cipher_;
};
```

**Decryption Pipeline:**
```
Encrypted File on Disk
    │
    ▼
Read Header (Magic, Version, Algorithm, Key ID, IV)
    │
    ▼
Fetch Decryption Key from Key Manager (using Key ID)
    │
    ▼
LEA-256-GCM Decrypt (key + IV + AAD)
    │
    ├─► Verify Auth Tag (16 bytes)
    │   └─► If invalid → File tampered, refuse playback
    │
    ▼
Decrypted H264 Bitstream (MKV container)
    │
    ▼
Feed to NVDEC Decoder
    │
    ▼
Display on Screen
```

#### **Key Management for Decryption**

```cpp
// src/core/crypto/key_manager.h
class KeyManager {
public:
    // Initialize with master key (from TPM/HSM or encrypted file)
    bool initialize(const std::vector<uint8_t>& masterKey);

    // Get decryption key by Key ID (from segment header)
    std::vector<uint8_t> getDecryptionKey(const std::string& keyId);

    // Key rotation: generate new DEK for tomorrow's recordings
    std::string rotateVideoDEK(const std::string& cameraId);

    // Export key for external playback (admin only, with audit log)
    std::vector<uint8_t> exportKey(const std::string& keyId,
                                   const std::string& adminPassword);

private:
    struct KeyRecord {
        std::string keyId;           // UUID
        std::vector<uint8_t> dek;    // Data Encryption Key (LEA-256)
        std::string cameraId;
        int64_t createdAt;
        int64_t expiresAt;
        bool revoked;
    };

    // Master Encryption Key (encrypts all DEKs)
    std::vector<uint8_t> mek_;

    // Key database (encrypted SQLite)
    std::map<std::string, KeyRecord> keys_;
    Database* keyDatabase_;
};
```

#### **Example: Playback Code**

```cpp
// src/client/playback/playback_engine.cpp
void PlaybackEngine::play(const std::string& cameraId,
                          int64_t startTime,
                          int64_t endTime) {
    // Query segments from database
    auto segments = database_->getSegments(cameraId, startTime, endTime);

    for (const auto& seg : segments) {
        // Open encrypted segment
        EncryptedSegmentReader reader;
        if (!reader.open(seg.filePath)) {
            LOG_ERROR("Failed to open segment: " << seg.filePath);
            continue;
        }

        // Read header
        SegmentHeader header;
        reader.readHeader(header);

        // Get decryption key from Key Manager
        auto key = keyManager_->getDecryptionKey(header.keyId);
        reader.setDecryptionKey(key);

        // Decrypt and decode frames
        NalUnit nal;
        while (reader.readNalUnit(nal)) {
            decoder_->decode(nal.data, nal.size, nal.pts);

            GPUFrame frame;
            if (decoder_->getDecodedFrame(frame)) {
                renderFrame(frame);
            }
        }

        reader.close();
    }
}
```

#### **Export Decryption** (Export to MP4)

```cpp
// src/recording/video_exporter.h
class VideoExporter {
public:
    // Export encrypted segments to unencrypted MP4
    bool exportToMP4(const std::vector<std::string>& segmentPaths,
                     const std::string& outputPath,
                     bool keepEncrypted = false);  // Optional re-encryption

private:
    bool decryptAndMux(const std::string& encryptedPath,
                      AVFormatContext* outputMuxer);
};
```

---

## 2. Filename Obfuscation Strategies

### Option A: No Extension (Recommended)
```
storage/
├── camera_01/
│   └── 20260126/
│       ├── 1738012200    ← No extension
│       ├── 1738012210
│       ├── 1738012220
│       └── ...
```

**Pros:**
- Completely hides file type
- Simple implementation
- Linux doesn't care about extensions

**Cons:**
- File manager won't show preview
- Manual MIME type detection needed

### Option B: Custom Extension (.fvd = FluxVision Data)
```
storage/
├── camera_01/
│   └── 20260126/
│       ├── 1738012200.fvd
│       ├── 1738012210.fvd
│       └── ...
```

**Pros:**
- Obfuscated but still has extension
- Easy to filter in file manager

**Cons:**
- .fvd can be Googled if attacker knows product name

### Option C: Hash-Based Filenames
```
storage/
├── camera_01/
│   └── 20260126/
│       ├── a3f5c8e2b9d14567    ← SHA256(camera_id + timestamp)[:16]
│       ├── b2e4d7f1a8c39210
│       └── ...
```

**Pros:**
- Maximum obfuscation
- No pattern recognition possible

**Cons:**
- Harder to debug manually
- Requires database lookup for everything

### Option D: Steganography (Advanced)
```
storage/
├── thumbnails/
│   └── thumb_001.jpg    ← Looks like JPEG, actually encrypted video!
│   └── thumb_002.jpg
```

**Pros:**
- Ultimate stealth (files look like innocent images)
- Confuses automated scanners

**Cons:**
- Complex implementation
- Slower file I/O

### **Recommended: Option A (No Extension) + Database Metadata**

```cpp
// src/recording/segment_writer.h (Updated)
class SegmentWriter {
public:
    struct SegmentMetadata {
        std::string filePath;        // "storage/camera_01/20260126/1738012200"
        std::string actualFormat;    // "mkv" (stored in DB, not filename)
        std::string encryptionAlgo;  // "LEA-256-GCM"
        std::string keyId;           // UUID for decryption
        int64_t startTime;
        int64_t endTime;
        int64_t fileSize;
    };

    // Generate filename without extension
    std::string generateFilename(const std::string& cameraId, int64_t timestamp) {
        // Format: camera_id/YYYYMMDD/unix_timestamp
        std::tm tm = *std::localtime(&timestamp);
        char dateDir[16];
        strftime(dateDir, sizeof(dateDir), "%Y%m%d", &tm);

        std::stringstream ss;
        ss << "storage/" << cameraId << "/" << dateDir << "/" << timestamp;
        return ss.str();  // NO extension!
    }

    // Store metadata in database
    void saveMetadata(const SegmentMetadata& meta) {
        database_->execute(
            "INSERT INTO recording_segments "
            "(file_path, format, encryption_algo, key_id, start_time, end_time, file_size) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            meta.filePath, meta.actualFormat, meta.encryptionAlgo,
            meta.keyId, meta.startTime, meta.endTime, meta.fileSize
        );
    }
};
```

### Database Schema Update
```sql
CREATE TABLE recording_segments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id TEXT NOT NULL,
    file_path TEXT NOT NULL UNIQUE,

    -- Hidden metadata (not in filename!)
    actual_format TEXT DEFAULT 'mkv',        -- File is MKV but filename doesn't show it
    encryption_algo TEXT DEFAULT 'LEA-256-GCM',
    key_id TEXT NOT NULL,                    -- For decryption

    start_time INTEGER NOT NULL,
    end_time INTEGER NOT NULL,
    file_size_bytes INTEGER,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    -- Index for fast lookups
    FOREIGN KEY (camera_id) REFERENCES cameras(id)
);

CREATE INDEX idx_segments_lookup ON recording_segments(camera_id, start_time, end_time);
CREATE INDEX idx_segments_keyid ON recording_segments(key_id);
```

### Security Through Obscurity Benefits

1. **Attacker doesn't know file type** → Can't easily identify what to target
2. **No obvious video files** → Automated malware skips unknown formats
3. **Mimetype detection fails** → `file` command returns "data"
4. **Requires insider knowledge** → Need access to database to understand structure

### Defense in Depth
```
Layer 1: Filename Obfuscation (no extension)
Layer 2: LEA-256-GCM Encryption (KCMVP certified)
Layer 3: Key Hierarchy (MEK → DEK with rotation)
Layer 4: File Permissions (0600, root-only access)
Layer 5: Database Encryption (ARIA-256 for metadata)
Layer 6: Audit Logging (ECDSA-signed access logs)
```

**Result:** Even if attacker gets file access, they see:
```bash
$ ls storage/camera_01/20260126/
1738012200  1738012210  1738012220  1738012230
$ file 1738012200
1738012200: data
$ cat 1738012200
FVMSÿþý�^@��[binary garbage]
```

No indication it's video. No way to decrypt without:
1. Database access (to get Key ID)
2. Key Manager access (to get decryption key)
3. Valid session credentials (KCMVP authentication)

### Performance Impact

| Operation | Without Encryption | With LEA-256-GCM | Overhead |
|-----------|-------------------|------------------|----------|
| 1080p Recording (1 cam) | 100% | ~102% | +2% CPU |
| 1080p Recording (42 cam) | 100% | ~105% | +5% CPU |
| Database Write | 100% | ~108% | +8% |
| Playback Decrypt | 100% | ~103% | +3% |

**Note**: LEA chosen over ARIA for video due to 1.5-2x better throughput on x86-64 (LEA optimized for 64-bit operations).

---

## Multi-Client / Multi-Server Architecture

### Overview

FluxVision VMS supports two critical deployment patterns similar to NX Witness:

1. **One Server → Multiple Clients**: Single recording server monitored by multiple independent client workstations
2. **One Client → Multiple Servers**: Single client workstation monitoring multiple recording servers with tab-based switching

### Architecture Pattern 1: Multiple Clients → One Server

```
┌────────────────────────────────────────────────────────────────────┐
│                        Server (GPU Workstation)                    │
│                    42 Cameras Recording + Decoding                 │
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│  Camera Ingestion & Decode (NVDEC)                                │
│  ┌──────────────────────────────────────────────────────────┐     │
│  │  Camera 1-42 → RTSP → Decode → GPU Memory (Shared)      │     │
│  └──────────────────────────────────────────────────────────┘     │
│                              │                                     │
│                              ▼                                     │
│  ┌──────────────────────────────────────────────────────────┐     │
│  │              Stream Broadcast Manager                    │     │
│  │    (Decode once, distribute to all clients)             │     │
│  └──────────────────────────────────────────────────────────┘     │
│           │              │              │              │          │
│           ▼              ▼              ▼              ▼          │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────┐  │
│  │  Client 1    │ │  Client 2    │ │  Client 3    │ │Client N│  │
│  │  Session     │ │  Session     │ │  Session     │ │Session │  │
│  │              │ │              │ │              │ │        │  │
│  │ Subscribed:  │ │ Subscribed:  │ │ Subscribed:  │ │Sub: All│  │
│  │ Cam 1-16     │ │ Cam 10-25    │ │ Cam 30-42    │ │42 cams │  │
│  │ Quality: GRID│ │ Quality: MIX │ │ Quality: FULL│ │Q: THUMB│  │
│  └──────────────┘ └──────────────┘ └──────────────┘ └────────┘  │
│           │              │              │              │          │
└───────────┼──────────────┼──────────────┼──────────────┼──────────┘
            │              │              │              │
            ▼              ▼              ▼              ▼
    ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐
    │ Monitor 1  │ │ Monitor 2  │ │ Monitor 3  │ │ Monitor N  │
    │ (Control   │ │ (Security  │ │ (Manager   │ │ (Mobile)   │
    │  Room)     │ │  Desk)     │ │  Office)   │ │            │
    └────────────┘ └────────────┘ └────────────┘ └────────────┘
```

**Key Features:**
- **Decode Once, Broadcast Many**: Server decodes each camera once, broadcasts to all connected clients
- **Independent Client Sessions**: Each client has separate authentication, camera selection, quality settings
- **Per-Client Quality Control**: Client 1 requests grid view (480p), Client 2 requests fullscreen (1080p) independently
- **No Client Interference**: Adding/removing clients doesn't affect others
- **Bandwidth Optimization**: Server only sends frames client has subscribed to
- **Resource Efficiency**: Server CPU/GPU usage scales with camera count, NOT client count

### Architecture Pattern 2: One Client → Multiple Servers

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Client Workstation (Monitor)                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌───────────────────────────────────────────────────────────┐     │
│  │            Server Tab Bar (NX Witness Style)              │     │
│  │  [Office HQ] [Warehouse A] [Parking Lot] [Branch B] [+]  │     │
│  └───────────────────────────────────────────────────────────┘     │
│                              │                                      │
│                              ▼                                      │
│  ┌───────────────────────────────────────────────────────────┐     │
│  │              Connection Pool Manager                      │     │
│  │                                                           │     │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │     │
│  │  │ Server 1    │  │ Server 2    │  │ Server N    │      │     │
│  │  │ 192.168.1.10│  │ 192.168.1.20│  │ 10.0.0.50   │      │     │
│  │  │ 42 cameras  │  │ 24 cameras  │  │ 16 cameras  │      │     │
│  │  │ ✓ Connected │  │ ✓ Connected │  │ ⚠ Reconnect │      │     │
│  │  └─────────────┘  └─────────────┘  └─────────────┘      │     │
│  └───────────────────────────────────────────────────────────┘     │
│                              │                                      │
│                              ▼                                      │
│  ┌───────────────────────────────────────────────────────────┐     │
│  │         Active Server Camera Grid (Office HQ)             │     │
│  │                                                           │     │
│  │  [Cam1] [Cam2] [Cam3] [Cam4]  ← 42 cameras from Server 1 │     │
│  │  [Cam5] [Cam6] [Cam7] [Cam8]                             │     │
│  │  ...                                                      │     │
│  └───────────────────────────────────────────────────────────┘     │
│                                                                     │
│  Switch to "Warehouse A" tab → Show 24 cameras from Server 2       │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
         │              │              │
         ▼              ▼              ▼
   ┌──────────┐   ┌──────────┐   ┌──────────┐
   │ Server 1 │   │ Server 2 │   │ Server N │
   │ Office HQ│   │Warehouse │   │ Branch B │
   │ :8080    │   │ :8080    │   │ :8080    │
   └──────────┘   └──────────┘   └──────────┘
```

**Key Features:**
- **Tab-Based Server Switching**: Like NX Witness, each tab represents one server
- **Persistent Connections**: All servers stay connected in background
- **Unified Interface**: Same UI for all servers, just different camera sets
- **Cross-Server Features**:
  - Unified search across all servers
  - Export from multiple servers
  - Synchronized playback (if timestamp aligned)
- **Server Discovery**: Auto-discover servers on LAN via mDNS/Zeroconf
- **Favorites/Groups**: Organize servers into folders (HQ Sites, Remote Sites, etc.)

---

### Multi-Client Server Architecture (Detailed)

#### Session Management

```cpp
// src/server/session_manager.h
class ClientSession {
public:
    struct Info {
        std::string sessionId;           // UUID
        std::string clientIp;
        std::string username;
        UserRole role;                   // ADMIN, OPERATOR, VIEWER
        int64_t connectedAt;
        int64_t lastActivity;

        // Per-client subscription
        std::vector<std::string> subscribedCameras;
        std::map<std::string, StreamQuality> cameraQualities;
    };

    // WebSocket connection for this client
    WebSocket* socket_;

    // Client-specific settings
    bool notificationsEnabled = true;
    bool audioEnabled = false;

    // Bandwidth management
    int maxBandwidthKbps = 50000;  // 50Mbps limit per client
    int currentBandwidthKbps = 0;
};

class SessionManager {
public:
    // Create new client session after auth
    std::string createSession(const std::string& username,
                             const std::string& clientIp,
                             WebSocket* socket);

    // Update client's camera subscriptions
    void updateSubscription(const std::string& sessionId,
                          const std::vector<std::string>& cameraIds,
                          const std::map<std::string, StreamQuality>& qualities);

    // Broadcast frame to all subscribed clients
    void broadcastFrame(const std::string& cameraId, const GPUFrame& frame);

    // Get all active sessions (for admin monitoring)
    std::vector<ClientSession::Info> getActiveSessions() const;

    // Kick client (admin function)
    void terminateSession(const std::string& sessionId);

private:
    std::map<std::string, std::unique_ptr<ClientSession>> sessions_;
    std::shared_mutex mutex_;
};
```

#### Broadcast Strategy (Decode Once, Send Many)

```cpp
// src/server/stream_broadcaster.h
class StreamBroadcaster {
public:
    // Called when new frame decoded
    void onFrameDecoded(const std::string& cameraId, const GPUFrame& frame) {
        // Get all clients subscribed to this camera
        auto clients = sessionManager_->getSubscribedClients(cameraId);

        // For each quality level needed
        std::map<StreamQuality, std::vector<ClientSession*>> qualityGroups;
        for (auto* client : clients) {
            auto quality = client->cameraQualities[cameraId];
            qualityGroups[quality].push_back(client);
        }

        // Encode once per quality level (not per client!)
        for (auto& [quality, clientGroup] : qualityGroups) {
            // Encode frame at this quality
            std::vector<uint8_t> encoded = encodeFrame(frame, quality);

            // Broadcast to all clients needing this quality
            for (auto* client : clientGroup) {
                sendFrameToClient(client, cameraId, encoded);
            }
        }
    }

private:
    std::vector<uint8_t> encodeFrame(const GPUFrame& frame, StreamQuality quality) {
        // Use NVENC for GPU encoding (1080p, 720p, 480p, 240p)
        // Or forward H264 bitstream if quality matches source
    }
};
```

#### Client Session Protocol

**WebSocket Message Format (JSON):**

```javascript
// Client → Server: Subscribe to cameras
{
    "type": "subscribe",
    "cameras": [
        {
            "id": "camera_01",
            "quality": "GRID_VIEW",  // THUMBNAIL, GRID_VIEW, FOCUSED, FULLSCREEN
            "fps": 15
        },
        {
            "id": "camera_15",
            "quality": "FULLSCREEN",
            "fps": 30
        }
    ]
}

// Server → Client: Frame delivery
{
    "type": "frame",
    "cameraId": "camera_01",
    "timestamp": 1738012345678,
    "format": "h264",
    "data": "<base64 encoded H264 NAL units>"
}

// Server → Client: Camera event
{
    "type": "event",
    "cameraId": "camera_05",
    "event": "motion_detected",
    "timestamp": 1738012345678,
    "metadata": { "region": "zone_1" }
}

// Client → Server: PTZ control (if camera supports)
{
    "type": "ptz",
    "cameraId": "camera_02",
    "action": "move",
    "pan": 45,
    "tilt": -10,
    "zoom": 2.5
}
```

---

### Multi-Server Client Architecture (Detailed)

#### Server Connection Pool

```cpp
// src/client/server_connection_pool.h
class ServerConnection {
public:
    struct Config {
        std::string serverId;        // UUID
        std::string name;            // "Office HQ"
        std::string host;            // "192.168.1.10"
        int port = 8080;
        std::string username;
        std::string password;
        bool autoConnect = true;
        int priority = 0;            // For auto-reconnect ordering
    };

    enum class State {
        DISCONNECTED,
        CONNECTING,
        AUTHENTICATING,
        CONNECTED,
        ERROR
    };

    // Connection lifecycle
    bool connect();
    void disconnect();
    State getState() const;

    // Camera operations
    std::vector<CameraInfo> getCameras();
    void subscribeToCameras(const std::vector<std::string>& cameraIds);

    // Recording queries
    std::vector<RecordingSegment> queryRecordings(
        const std::string& cameraId,
        int64_t startTime,
        int64_t endTime
    );

private:
    Config config_;
    State state_ = State::DISCONNECTED;
    WebSocket* socket_ = nullptr;
    std::string sessionToken_;
};

class ServerConnectionPool {
public:
    // Add server to pool
    std::string addServer(const ServerConnection::Config& config);

    // Remove server
    void removeServer(const std::string& serverId);

    // Get all servers
    std::vector<ServerConnection*> getAllServers();

    // Get active server (currently viewed tab)
    ServerConnection* getActiveServer() const;

    // Set active server (user switched tab)
    void setActiveServer(const std::string& serverId);

    // Auto-reconnect management
    void enableAutoReconnect(bool enabled);

private:
    std::map<std::string, std::unique_ptr<ServerConnection>> servers_;
    std::string activeServerId_;
    std::thread reconnectThread_;
};
```

#### Tab-Based UI

```cpp
// src/client/widgets/server_tab_widget.h
class ServerTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit ServerTabWidget(QWidget* parent = nullptr);

    // Add server tab
    void addServerTab(const std::string& serverId, const std::string& name);

    // Remove server tab
    void removeServerTab(const std::string& serverId);

    // Update tab status (show connection state)
    void updateTabStatus(const std::string& serverId,
                        ServerConnection::State state);

signals:
    void serverTabChanged(const std::string& serverId);
    void serverAdded(const std::string& serverId);
    void serverRemoved(const std::string& serverId);

protected:
    void currentChanged(int index) override;

private:
    std::map<std::string, int> serverToTabIndex_;
};

// Each tab contains a full camera grid for that server
class ServerCameraGridWidget : public QWidget {
public:
    void setServer(ServerConnection* server);
    void loadCameras();  // Load cameras from this server
    void startStreaming();
    void stopStreaming();

private:
    ServerConnection* server_;
    CameraGridWidget* gridWidget_;
};
```

#### Server Discovery (mDNS/Zeroconf)

```cpp
// src/client/server_discovery.h
class ServerDiscovery {
public:
    struct DiscoveredServer {
        std::string name;
        std::string host;
        int port;
        std::string version;
        int cameraCount;
        bool isOnline;
    };

    // Start/stop discovery
    void startDiscovery();
    void stopDiscovery();

    // Get discovered servers on LAN
    std::vector<DiscoveredServer> getDiscoveredServers() const;

signals:
    void serverDiscovered(const DiscoveredServer& server);
    void serverLost(const std::string& host);

private:
    // mDNS service: _fluxvision._tcp.local
    void publishmDNS();
    void browsemDNS();
};
```

---

### Resource Management with Multiple Clients

#### Server-Side Resource Allocation

```cpp
// Maximum simultaneous clients
constexpr int MAX_CLIENTS = 16;

// Per-client bandwidth limits
struct ClientBandwidthPolicy {
    int maxBandwidthMbps = 50;         // Total per client
    int maxCamerasFullscreen = 4;      // Max 4 cameras at 1080p
    int maxCamerasTotal = 42;          // Can view all cameras
};

// Server resource monitoring
struct ServerResources {
    int totalClients = 0;
    int totalOutgoingBandwidthMbps = 0;
    int encoderSessions = 0;           // NVENC sessions for client streaming

    // Alert if:
    // - Total bandwidth > 500Mbps (1Gbps NIC saturated)
    // - Encoder sessions > 16 (NVENC limit)
    // - Clients > 16 (arbitrary limit)
};
```

#### Client-Side Resource Allocation (Multi-Server)

```cpp
// Maximum simultaneous server connections
constexpr int MAX_SERVERS = 8;

// Total bandwidth across all servers
constexpr int MAX_TOTAL_BANDWIDTH_MBPS = 100;

// Active vs background servers
// Active server (visible tab): Full quality
// Background servers (hidden tabs): Pause or thumbnail only
struct ServerResourcePolicy {
    bool pauseBackgroundServers = true;   // Pause streams when tab hidden
    bool keepBackgroundConnected = true;  // Maintain WebSocket connection
    int backgroundThumbnailFps = 1;       // 1 FPS for background thumbnails
};
```

---

### Authentication & Authorization (Multi-Client)

#### User Roles

```cpp
enum class UserRole {
    ADMIN,      // Full control: add/remove cameras, manage users, view all
    OPERATOR,   // View all cameras, control PTZ, export recordings
    VIEWER      // View assigned cameras only, no control
};

struct UserPermissions {
    UserRole role;
    std::vector<std::string> allowedCameras;  // Empty = all cameras
    bool canExport = false;
    bool canControlPTZ = false;
    bool canViewArchive = true;
    bool canViewLive = true;
};
```

#### Session Authentication Flow

```
Client                          Server
  │                               │
  ├──► WebSocket Connect          │
  │                               │
  ├──► {"type":"auth",            │
  │      "username":"operator1",  │
  │      "password":"***"}        │
  │                               │
  │    ◄──── PBKDF2 Verify ───────┤
  │                               │
  │    ◄──── ECDH Key Exchange ───┤
  │                               │
  │   ◄─── {"type":"auth_ok",     │
  │         "sessionId":"uuid",   │
  │         "permissions":{...}}  │
  │                               │
  ├──► Encrypted Subscribe Req    │
  │                               │
  │   ◄─── Encrypted Frames ──────┤
  │                               │
```

---

### Performance Characteristics

#### One Server → Multiple Clients

| Clients | Decode CPU | Encode CPU (NVENC) | Network Out | Server Load |
|---------|------------|-------------------|-------------|-------------|
| 1       | 15%        | 2%                | 50 Mbps     | Baseline    |
| 4       | 15%        | 5%                | 200 Mbps    | +3% CPU     |
| 8       | 15%        | 8%                | 400 Mbps    | +5% CPU     |
| 16      | 15%        | 12%               | 800 Mbps    | +8% CPU     |

**Key Insight**: Decode cost is fixed (one decode per camera). Encoding cost scales linearly with client count, but NVENC is efficient (1-2% per client).

#### One Client → Multiple Servers

| Servers | Network In | Decode CPU | Render GPU | RAM    |
|---------|-----------|------------|------------|--------|
| 1       | 50 Mbps   | 10%        | 25%        | 600MB  |
| 2       | 100 Mbps  | 18%        | 28%        | 1.1GB  |
| 4       | 200 Mbps  | 32%        | 32%        | 2.0GB  |
| 8       | 400 Mbps  | 58%        | 38%        | 3.8GB  |

**Mitigation**: Pause background servers when not viewing their tab (reduces to ~5 Mbps per background server for thumbnails only).

---

## Cross-Platform Architecture

### Supported Platforms

| Platform | Compiler | CUDA | Qt | KCMVP Crypto | Build Status |
|----------|----------|------|-----|--------------|--------------|
| **Linux x64** | GCC 9+ / Clang 10+ | 12.0+ | Qt5 | KryptoAPI / OpenSSL 3.0 | Primary |
| **Windows 10/11 x64** | MSVC 2019/2022 | 12.0+ | Qt5 | OpenSSL 3.0 + Custom LEA | Secondary |

### Platform Abstraction Layer

```cpp
// src/common/platform/platform.h
#pragma once

#ifdef _WIN32
    #define PLATFORM_WINDOWS
    #include <windows.h>
    #define PATH_SEPARATOR '\\'
    #define LINE_ENDING "\r\n"
#elif __linux__
    #define PLATFORM_LINUX
    #include <unistd.h>
    #include <sys/stat.h>
    #define PATH_SEPARATOR '/'
    #define LINE_ENDING "\n"
#endif

namespace Platform {
    // File system operations (cross-platform)
    std::string normalizePath(const std::string& path);
    bool createDirectory(const std::string& path);
    bool fileExists(const std::string& path);
    uint64_t getFileSize(const std::string& path);

    // Process management
    int getCurrentProcessId();
    void setProcessPriority(int priority);

    // Service/Daemon
    bool installService(const std::string& name);
    bool uninstallService(const std::string& name);
    void runAsService();
}
```

### Platform-Specific Implementations

#### File Paths (Cross-Platform)
```cpp
// src/common/platform/filesystem.cpp
#include <filesystem>  // C++17

std::string Platform::normalizePath(const std::string& path) {
    std::filesystem::path p(path);
    return p.make_preferred().string();  // Auto-converts / to \ on Windows
}

// Usage:
std::string segmentPath = Platform::normalizePath(
    "storage/camera_01/20260126/1738012200"
);
// Linux: storage/camera_01/20260126/1738012200
// Windows: storage\camera_01\20260126\1738012200
```

#### Service/Daemon
```cpp
// src/server/service/service_manager.h
class ServiceManager {
public:
    bool install();
    bool uninstall();
    void run();

private:
#ifdef PLATFORM_WINDOWS
    void runWindowsService();
    static void WINAPI serviceMain(DWORD argc, LPTSTR* argv);
    static void WINAPI serviceCtrlHandler(DWORD ctrl);
#elif PLATFORM_LINUX
    void runLinuxDaemon();
    void daemonize();
    void setupSignalHandlers();
#endif
};
```

### KCMVP Crypto Cross-Platform

#### Linux Implementation (KryptoAPI)
```cpp
// src/core/crypto/linux/kcmvp_linux.cpp
#include <kryptoapi/lea.h>    // KryptoAPI for LEA (KCMVP-certified)
#include <kryptoapi/aria.h>   // KryptoAPI for ARIA
#include <openssl/evp.h>       // OpenSSL for ECDH/HKDF

class KCMVPCryptoLinux : public IKCMVPCrypto {
public:
    std::vector<uint8_t> leaEncrypt(const std::vector<uint8_t>& plaintext,
                                   const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv) override {
        LEA_KEY leaKey;
        LEA_set_key(&leaKey, key.data(), 256);
        // ... LEA-GCM encryption
    }
};
```

#### Windows Implementation (OpenSSL + Portable LEA)
```cpp
// src/core/crypto/windows/kcmvp_windows.cpp
#include <openssl/evp.h>       // OpenSSL 3.0 for ARIA
#include "crypto/lea/lea_portable.h"   // Custom portable LEA

class KCMVPCryptoWindows : public IKCMVPCrypto {
public:
    // ARIA via OpenSSL 3.0 (built-in support)
    std::vector<uint8_t> ariaEncrypt(const std::vector<uint8_t>& plaintext,
                                    const std::vector<uint8_t>& key,
                                    const std::vector<uint8_t>& iv) override {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aria_256_gcm(), NULL, key.data(), iv.data());
        // ... ARIA-GCM encryption
    }

    // LEA via portable C++ implementation
    std::vector<uint8_t> leaEncrypt(const std::vector<uint8_t>& plaintext,
                                   const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv) override {
        LEAPortable lea;
        lea.setKey(key.data(), 256);
        return lea.encryptGCM(plaintext.data(), plaintext.size(),
                             key.data(), iv.data(), iv.size(),
                             nullptr, 0, tag);
    }
};
```

#### Portable LEA Implementation (C++, both platforms)
```cpp
// src/core/crypto/lea/lea_portable.h
// Pure C++ implementation of LEA-256-GCM (TTAK.KO-12.0223)
// Works on any platform without external dependencies

class LEAPortable {
public:
    static constexpr int KEY_SIZE = 32;    // 256-bit
    static constexpr int BLOCK_SIZE = 16;  // 128-bit

    void setKey(const uint8_t* key, int keyBits);
    void encryptBlock(const uint8_t* in, uint8_t* out);
    void decryptBlock(const uint8_t* in, uint8_t* out);

    // GCM mode
    std::vector<uint8_t> encryptGCM(
        const uint8_t* plaintext, size_t len,
        const uint8_t* key,
        const uint8_t* iv, size_t ivLen,
        const uint8_t* aad, size_t aadLen,
        uint8_t* tag
    );

private:
    uint32_t roundKeys_[192];  // 24 rounds * 8 words
    void keySchedule(const uint8_t* key, int keyBits);
};
```

### CMake Build Configuration

```cmake
# CMakeLists.txt (Root)
cmake_minimum_required(VERSION 3.18)
project(FluxVisionVMS LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)

# Platform detection
if(WIN32)
    set(PLATFORM_NAME "Windows")
    add_definitions(-DPLATFORM_WINDOWS)

    # Force MSVC on Windows
    if(NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC compiler for CUDA compatibility")
    endif()

    # Windows-specific flags
    add_compile_options(/W4 /MP)  # Warning level 4, Multi-processor compilation
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "Linux")
    add_definitions(-DPLATFORM_LINUX)

    # Linux-specific flags
    add_compile_options(-Wall -Wextra -pthread)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")

endif()

message(STATUS "Building for ${PLATFORM_NAME}")

# Find CUDA Toolkit (same on both platforms)
find_package(CUDAToolkit 12.0 REQUIRED)

# Find Qt5 (same on both platforms)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets OpenGL)

# Find OpenSSL 3.0+ (for ARIA on both, ECDH/HKDF)
find_package(OpenSSL 3.0 REQUIRED)

# Platform-specific crypto libraries
if(UNIX)
    # Try to find KryptoAPI on Linux (optional but preferred for KCMVP cert)
    find_library(KRYPTOAPI_LIB NAMES kryptoapi)
    if(KRYPTOAPI_LIB)
        message(STATUS "Found KryptoAPI (KCMVP-certified): ${KRYPTOAPI_LIB}")
        add_definitions(-DUSE_KRYPTOAPI)
    else()
        message(WARNING "KryptoAPI not found, will use portable LEA implementation")
        add_definitions(-DUSE_PORTABLE_LEA)
    endif()
elseif(WIN32)
    # Windows always uses portable LEA + OpenSSL ARIA
    add_definitions(-DUSE_PORTABLE_LEA)
    message(STATUS "Using portable LEA implementation + OpenSSL ARIA")
endif()

# Add subdirectories
add_subdirectory(src/core)
add_subdirectory(src/server)
add_subdirectory(src/client)
add_subdirectory(src/recording)
add_subdirectory(src/common)
```

### Platform-Specific Module Structure

```
src/
├── common/
│   ├── platform/
│   │   ├── platform.h             # Platform abstraction interface
│   │   ├── filesystem.cpp         # Cross-platform (std::filesystem)
│   │   ├── linux/
│   │   │   ├── daemon.cpp         # Linux daemon
│   │   │   └── signals.cpp        # Signal handlers
│   │   └── windows/
│   │       ├── service.cpp        # Windows service
│   │       └── registry.cpp       # Registry access
│   └── crypto/
│       ├── kcmvp_interface.h      # Crypto interface
│       ├── lea/
│       │   └── lea_portable.cpp   # Portable LEA impl (both platforms)
│       ├── linux/
│       │   └── kcmvp_linux.cpp    # KryptoAPI wrapper
│       └── windows/
│           └── kcmvp_windows.cpp  # OpenSSL + portable LEA
```

### Build Instructions

#### Linux Build
```bash
# Install dependencies
sudo apt install build-essential cmake qt5-default \
    libqt5opengl5-dev libssl-dev libsqlite3-dev libyaml-cpp-dev \
    nvidia-cuda-toolkit

# Optional: Install KryptoAPI (KCMVP-certified)
# Download from https://seed.kisa.or.kr/

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install
sudo make install

# Install as systemd service
sudo ./scripts/install_service_linux.sh
```

#### Windows Build
```batch
REM Prerequisites:
REM - Visual Studio 2022 with C++ and CUDA workload
REM - CUDA Toolkit 12.0+ from developer.nvidia.com
REM - Qt 5.15.2 MSVC 2019 64-bit from qt.io
REM - OpenSSL 3.0 Win64 from slproweb.com/products/Win32OpenSSL.html

REM Build
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" ^
    -DOPENSSL_ROOT_DIR="C:\OpenSSL-Win64"

cmake --build . --config Release --parallel

REM Install as Windows Service
scripts\install_service_windows.bat
```

### Deployment Packages

#### Linux Package
```
fluxvision-vms-2.0.0-linux-x64.tar.gz
├── bin/
│   ├── fluxvision-server
│   └── fluxvision-client
├── lib/
│   ├── libfluxvision-core.so
│   └── libfluxvision-crypto.so
├── share/
│   ├── systemd/fluxvision.service
│   └── config/config.yaml
└── install.sh
```

#### Windows Package
```
FluxVisionVMS-2.0.0-Win64.zip
├── bin/
│   ├── FluxVisionServer.exe
│   ├── FluxVisionClient.exe
│   ├── Qt5Core.dll
│   ├── Qt5Widgets.dll
│   ├── Qt5OpenGL.dll
│   ├── cudart64_12.dll
│   └── nvcuvid.dll
├── config/
│   └── config.yaml
└── install_service.bat
```

### Platform-Specific Dependencies Summary

| Component | Linux | Windows |
|-----------|-------|---------|
| **Compiler** | GCC 9+ / Clang 10+ | MSVC 2019/2022 (required) |
| **CUDA** | CUDA Toolkit 12.0+ | CUDA Toolkit 12.0+ |
| **Qt** | qt5-default | Qt 5.15.2 MSVC build |
| **OpenSSL** | libssl-dev (3.0+) | Win64OpenSSL 3.0+ |
| **LEA** | KryptoAPI (optional) | Portable C++ impl |
| **ARIA** | KryptoAPI or OpenSSL | OpenSSL 3.0 |
| **SQLite** | libsqlite3-dev | Bundled |
| **YAML** | libyaml-cpp-dev | vcpkg yaml-cpp |

---

## Phase 0: Foundation Setup (Days 1-2)

### Objectives
- Set up project structure
- Configure build system
- Verify hardware capabilities
- Run benchmarks to establish baseline

### Directory Structure
```
VMS/
├── CMakeLists.txt                 # Root CMake
├── cmake/                         # CMake modules
│   ├── FindNVDEC.cmake
│   ├── FindCUDA.cmake
│   └── CompilerFlags.cmake
├── src/
│   ├── core/                      # Core engine (no Qt dependency)
│   │   ├── CMakeLists.txt
│   │   ├── codec/                 # Decoders
│   │   ├── network/               # RTSP/RTP
│   │   ├── gpu/                   # GPU memory management
│   │   ├── threading/             # Thread pools
│   │   └── stream/                # Stream management
│   ├── recording/                 # Recording engine
│   │   ├── CMakeLists.txt
│   │   ├── segment_writer.h/cpp
│   │   ├── mkv_muxer.h/cpp
│   │   └── recording_manager.h/cpp
│   ├── client/                    # Qt UI
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── main_window.h/cpp
│   │   ├── widgets/
│   │   └── rendering/
│   └── common/                    # Shared types
│       ├── CMakeLists.txt
│       ├── types.h
│       ├── config.h/cpp
│       └── logger.h/cpp
├── tests/                         # Unit tests
├── tools/                         # Benchmarks, utilities
├── config/                        # Configuration files
│   └── config.yaml
└── docs/                          # Documentation
```

### Deliverables
- [ ] Project skeleton created
- [ ] CMake build system working
- [ ] CUDA/NVDEC availability verified
- [ ] Baseline benchmark completed (v1 performance numbers)

### Tasks

```
0.1 Create directory structure
0.2 Set up CMakeLists.txt with CUDA, Qt5, FFmpeg detection
0.3 Create hardware detection utility (NVDEC capabilities)
0.4 Run v1 benchmark: 42 cameras, measure CPU/RAM/GPU/VRAM
0.5 Document baseline numbers for comparison
```

---

## Phase 1: Core Decoder Engine (Days 3-7)

### Objectives
- Implement NVDEC hardware decoder
- Create decoder abstraction for multiple backends
- Achieve single-camera decode with <3% CPU

### Key Components

#### 1.1 Decoder Interface
```cpp
// src/core/codec/decoder_interface.h
class IDecoder {
public:
    virtual ~IDecoder() = default;

    struct Config {
        int width = 1920;
        int height = 1080;
        CodecType codec = CodecType::H264;
        int numSurfaces = 8;  // Decode surface pool
    };

    virtual bool initialize(const Config& config) = 0;
    virtual bool decode(const uint8_t* data, size_t size, int64_t pts) = 0;
    virtual bool getDecodedFrame(GPUFrame& frame) = 0;
    virtual void flush() = 0;
    virtual DecoderStats getStats() const = 0;
};
```

#### 1.2 NVDEC Implementation
```cpp
// src/core/codec/nvdec_decoder.h
class NvdecDecoder : public IDecoder {
public:
    bool initialize(const Config& config) override;
    bool decode(const uint8_t* data, size_t size, int64_t pts) override;
    bool getDecodedFrame(GPUFrame& frame) override;

private:
    CUvideodecoder decoder_ = nullptr;
    CUvideoparser parser_ = nullptr;
    CUcontext cudaContext_ = nullptr;

    // Surface pool for decoded frames
    std::vector<CUdeviceptr> surfaces_;
    LockFreeQueue<GPUFrame> decodedFrames_;
};
```

#### 1.3 GPU Frame Structure
```cpp
// src/common/types.h
struct GPUFrame {
    CUdeviceptr cudaPtr = 0;      // CUDA device pointer
    int width = 0;
    int height = 0;
    int pitch = 0;
    int64_t pts = 0;
    PixelFormat format = PixelFormat::NV12;

    // For OpenGL interop
    GLuint glTexture = 0;
    CUgraphicsResource cudaResource = nullptr;
};
```

### Deliverables
- [ ] IDecoder interface defined
- [ ] NvdecDecoder working with single camera
- [ ] CPU decoder fallback (FFmpeg/libavcodec)
- [ ] Decoder factory with auto-detection
- [ ] Unit tests for decoder

### Validation
```
Test: Decode 1080p stream for 60 seconds
Expected:
- CPU: <3%
- VRAM: <300MB
- Frame rate: 25-30 fps stable
- No frame drops
```

---

## Phase 2: Network Layer (Days 8-14)

### Objectives
- Implement lightweight RTSP client (no GStreamer)
- RTP packet parsing and NAL unit extraction
- Support both main and sub streams
- Connection pooling and reconnection logic

### Key Components

#### 2.1 RTSP Client
```cpp
// src/core/network/rtsp_client.h
class RtspClient {
public:
    struct Config {
        std::string url;
        std::string username;
        std::string password;
        TransportType transport = TransportType::TCP;  // TCP preferred
        int timeout_ms = 5000;
        bool enableSubStream = true;
    };

    bool connect(const Config& config);
    void disconnect();

    // Returns raw RTP packets
    bool receivePacket(RtpPacket& packet);

    // Stream profile switching
    bool switchToMainStream();
    bool switchToSubStream();

    ConnectionState getState() const;

private:
    // Use FFmpeg's libavformat for RTSP (lightweight, no GStreamer)
    AVFormatContext* formatCtx_ = nullptr;
};
```

#### 2.2 RTP Depacketizer
```cpp
// src/core/network/rtp_depacketizer.h
class RtpDepacketizer {
public:
    // Input: RTP packets, Output: Complete NAL units
    bool addPacket(const RtpPacket& packet);
    bool getNalUnit(NalUnit& nalUnit);

private:
    // Handle fragmented NAL units (FU-A)
    std::vector<uint8_t> fragmentBuffer_;
    LockFreeQueue<NalUnit> nalUnits_;
};
```

#### 2.3 H264 Parser
```cpp
// src/core/network/h264_parser.h
class H264Parser {
public:
    struct NalInfo {
        NalUnitType type;  // SPS, PPS, IDR, P-frame, etc.
        bool isKeyframe;
        int temporalId;
    };

    NalInfo parseNalUnit(const uint8_t* data, size_t size);
    bool extractSPS(const NalUnit& nal, SPSInfo& sps);
    bool extractPPS(const NalUnit& nal, PPSInfo& pps);
};
```

### Deliverables
- [ ] RtspClient connecting to cameras
- [ ] RTP depacketizer extracting NAL units
- [ ] H264 parser identifying frame types
- [ ] Dual-stream support (main/sub profiles)
- [ ] Automatic reconnection on failure
- [ ] Unit tests for network layer

### Validation
```
Test: Connect to 10 cameras, receive for 60 seconds
Expected:
- All connections stable
- No packet loss
- CPU: <5% (parsing only, no decoding)
- Bandwidth: matches camera bitrate
```

---

## Phase 3: Threading & Memory Management (Days 15-21)

### Objectives
- Implement fixed-size thread pools
- Lock-free queues for packet/frame passing
- GPU memory pool with pre-allocated surfaces
- Workload balancing across decode threads

### Key Components

#### 3.1 Thread Pool
```cpp
// src/core/threading/thread_pool.h
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads, const std::string& name);
    ~ThreadPool();

    template<typename F>
    void submit(F&& task);

    void shutdown();
    ThreadPoolStats getStats() const;

private:
    std::vector<std::thread> workers_;
    LockFreeQueue<std::function<void()>> tasks_;
    std::atomic<bool> running_{true};
};

// Specialized pools
class NetworkThreadPool : public ThreadPool {
    // 8 threads for RTSP/RTP handling
};

class DecodeThreadPool : public ThreadPool {
    // 4-8 threads for NVDEC operations
};
```

#### 3.2 Lock-Free Queue
```cpp
// src/core/threading/lock_free_queue.h
template<typename T>
class LockFreeQueue {
public:
    bool push(T&& item);
    bool pop(T& item);
    bool empty() const;
    size_t size() const;

private:
    // MPSC (Multi-Producer, Single-Consumer) implementation
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};
```

#### 3.3 GPU Memory Pool
```cpp
// src/core/gpu/memory_pool.h
class GPUMemoryPool {
public:
    struct Config {
        int maxSurfaces = 256;        // For 42 cameras * 6 surfaces
        int surfaceWidth = 1920;
        int surfaceHeight = 1080;
    };

    bool initialize(const Config& config);

    GPUSurface* acquire();
    void release(GPUSurface* surface);

    MemoryPoolStats getStats() const;

private:
    std::vector<GPUSurface> surfaces_;
    LockFreeQueue<GPUSurface*> available_;
};
```

#### 3.4 Stream Manager (Per-Camera State)
```cpp
// src/core/stream/stream_manager.h
class StreamManager {
public:
    struct CameraState {
        std::string id;
        StreamQuality quality = StreamQuality::GRID_VIEW;
        ConnectionState connection = ConnectionState::DISCONNECTED;

        // Statistics
        int fps = 0;
        int droppedFrames = 0;
        int64_t lastFrameTime = 0;
    };

    void addCamera(const CameraConfig& config);
    void removeCamera(const std::string& id);

    void setQuality(const std::string& id, StreamQuality quality);
    CameraState getState(const std::string& id) const;

    // Called by decode thread pool
    void processFrame(const std::string& cameraId, GPUFrame&& frame);

private:
    std::unordered_map<std::string, CameraState> cameras_;
    std::shared_mutex mutex_;
};
```

### Thread Architecture (42 Cameras)
```
Network Pool (8 threads):
├── Thread 1: Cameras 1-6 (RTSP receive)
├── Thread 2: Cameras 7-12
├── ...
└── Thread 8: Cameras 37-42

Decode Pool (4 threads):
├── Thread 1: NVDEC session 1 (handles ~10 cameras)
├── Thread 2: NVDEC session 2
├── Thread 3: NVDEC session 3
└── Thread 4: NVDEC session 4

Render Thread (1):
└── OpenGL rendering (main thread)

Recording Pool (4 threads):
├── Thread 1: File I/O for cameras 1-10
├── ...
└── Thread 4: File I/O for cameras 31-42

Total: 17 threads (vs 100+ in v1)
```

### Deliverables
- [ ] ThreadPool implementation
- [ ] LockFreeQueue implementation
- [ ] GPUMemoryPool implementation
- [ ] StreamManager with camera state
- [ ] Integration test: 42 cameras through pipeline
- [ ] Resource monitoring (CPU, RAM, VRAM per component)

### Validation
```
Test: 42 cameras connected, decoding, 60 seconds
Expected:
- CPU: <20%
- RAM: <600MB
- VRAM: <3GB
- Thread count: <25
- No frame drops
```

---

## Phase 4: GPU Rendering (Days 22-28)

### Objectives
- CUDA-OpenGL interop for zero-copy rendering
- Texture atlas for efficient grid rendering
- Qt5 integration with QOpenGLWidget
- Adaptive quality based on tile size

### Key Components

#### 4.1 CUDA-OpenGL Interop
```cpp
// src/core/gpu/cuda_gl_interop.h
class CudaGLInterop {
public:
    bool initialize();

    // Register GL texture for CUDA access
    bool registerTexture(GLuint texture, CUgraphicsResource& resource);

    // Copy decoded frame to GL texture (zero-copy when possible)
    bool copyFrameToTexture(const GPUFrame& frame, GLuint texture);

private:
    CUcontext cudaContext_ = nullptr;
};
```

#### 4.2 Texture Atlas
```cpp
// src/core/gpu/texture_atlas.h
class TextureAtlas {
public:
    struct Config {
        int atlasWidth = 8192;   // Support up to 64 cameras (8x8 grid)
        int atlasHeight = 8192;
        int tileWidth = 1024;    // Each camera tile
        int tileHeight = 1024;
    };

    bool initialize(const Config& config);

    // Update specific tile with new frame
    void updateTile(int cameraIndex, const GPUFrame& frame);

    // Get texture coordinates for camera
    void getTileCoords(int cameraIndex, float& u0, float& v0, float& u1, float& v1);

    GLuint getAtlasTexture() const { return atlasTexture_; }

private:
    GLuint atlasTexture_ = 0;
    std::vector<CUgraphicsResource> tileResources_;
};
```

#### 4.3 GPU Renderer
```cpp
// src/client/rendering/gpu_renderer.h
class GPURenderer {
public:
    bool initialize(int maxCameras);

    // Update frame for specific camera
    void updateCameraFrame(int cameraIndex, const GPUFrame& frame);

    // Render entire grid in single draw call
    void renderGrid(const GridLayout& layout);

    // Render single camera fullscreen
    void renderFullscreen(int cameraIndex);

private:
    TextureAtlas atlas_;
    CudaGLInterop interop_;

    // Shaders for NV12 to RGB conversion
    GLuint shaderProgram_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};
```

#### 4.4 Adaptive Quality Controller
```cpp
// src/core/stream/quality_controller.h
class QualityController {
public:
    enum class Quality {
        PAUSED,      // Not visible, receive keyframes only
        THUMBNAIL,   // <100px, sub-stream @ 5fps
        GRID_VIEW,   // 100-400px, sub-stream @ 15fps
        FOCUSED,     // 400-800px, main-stream @ 25fps
        FULLSCREEN   // >800px, main-stream @ 30fps
    };

    Quality calculateQuality(int tileWidth, int tileHeight);

    struct QualitySettings {
        bool useMainStream;
        int targetFps;
        bool decodeAllFrames;  // false = keyframes only
    };

    QualitySettings getSettings(Quality quality);
};
```

### Rendering Pipeline
```
Decoded Frame (CUDA NV12)
    │
    ▼
CUDA-OpenGL Interop
    │
    ▼
Texture Atlas (single large texture)
    │
    ▼
OpenGL Shader (NV12→RGB conversion)
    │
    ▼
Single Draw Call (renders all tiles)
    │
    ▼
Qt QOpenGLWidget
```

### Deliverables
- [ ] CudaGLInterop working
- [ ] TextureAtlas with tile updates
- [ ] GPURenderer with grid/fullscreen modes
- [ ] NV12→RGB shader
- [ ] QualityController with 5 levels
- [ ] Integration test: smooth rendering

### Validation
```
Test: 42 cameras rendering in grid, resize window
Expected:
- Smooth 60fps rendering
- Quality adjusts based on tile size
- GPU: <40%
- No tearing or artifacts
```

---

## Phase 5: Qt UI Integration (Days 29-35)

### Objectives
- Main window with grid view
- Camera widgets with overlays
- Single/multi-camera selection
- Settings panel
- Fullscreen mode

### Key Components

#### 5.1 Main Window
```cpp
// src/client/main_window.h
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onCameraDoubleClicked(int cameraIndex);
    void onLayoutChanged(int rows, int cols);
    void onSettingsClicked();

private:
    CameraGridWidget* gridWidget_;
    SettingsDialog* settingsDialog_;
    QToolBar* toolbar_;
};
```

#### 5.2 Camera Grid Widget
```cpp
// src/client/widgets/camera_grid_widget.h
class CameraGridWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    void setCameras(const std::vector<CameraConfig>& cameras);
    void setLayout(int rows, int cols);

signals:
    void cameraClicked(int index);
    void cameraDoubleClicked(int index);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    GPURenderer renderer_;
    GridLayout layout_;
    int selectedCamera_ = -1;
};
```

#### 5.3 Camera Overlay
```cpp
// src/client/widgets/camera_overlay.h
class CameraOverlay {
public:
    struct Info {
        std::string name;
        std::string status;  // "LIVE", "RECORDING", "OFFLINE"
        int fps;
        int bitrate;
        bool showStats;
    };

    void render(const Info& info, const QRect& bounds);

private:
    QFont font_;
    QColor backgroundColor_;
};
```

### UI Layout
```
┌─────────────────────────────────────────────────────────────┐
│ [Logo] FluxVision VMS    [Layout ▼] [Settings] [Fullscreen]│
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐          │
│  │ Cam 01  │ │ Cam 02  │ │ Cam 03  │ │ Cam 04  │          │
│  │  LIVE   │ │  LIVE   │ │  LIVE   │ │  LIVE   │          │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘          │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐          │
│  │ Cam 05  │ │ Cam 06  │ │ Cam 07  │ │ Cam 08  │          │
│  │  LIVE   │ │  LIVE   │ │OFFLINE  │ │  LIVE   │          │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘          │
│                         ...                                │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│ Cameras: 42 | CPU: 18% | GPU: 35% | RAM: 650MB            │
└─────────────────────────────────────────────────────────────┘
```

### Deliverables
- [ ] MainWindow with toolbar
- [ ] CameraGridWidget with OpenGL rendering
- [ ] Camera overlays (name, status, FPS)
- [ ] Grid layout selector (2x2, 3x3, 4x4, 6x6, custom)
- [ ] Single camera fullscreen mode
- [ ] Settings dialog (server, cameras, quality)
- [ ] Status bar with system stats

### Validation
```
Test: Full UI with 42 cameras
Expected:
- Responsive UI (<16ms frame time)
- Smooth grid transitions
- Correct overlay rendering
- No memory leaks
```

---

## Phase 6: Recording Engine (Days 36-42)

### Objectives
- Direct H264 bitstream recording (no re-encoding)
- Segment-based storage (10-second chunks)
- SQLite metadata database
- Efficient disk I/O

### Key Components

#### 6.1 Segment Writer
```cpp
// src/recording/segment_writer.h
class SegmentWriter {
public:
    struct Config {
        std::string basePath;
        int segmentDuration = 10;  // seconds
        std::string containerFormat = "mkv";
    };

    bool initialize(const Config& config);

    // Write NAL unit directly (no re-encoding!)
    bool writeNalUnit(const NalUnit& nal, int64_t pts);

    // Force segment split
    void forceNewSegment();

    std::string getCurrentSegmentPath() const;

private:
    AVFormatContext* muxer_ = nullptr;
    std::string currentSegmentPath_;
    int64_t segmentStartTime_ = 0;
};
```

#### 6.2 Recording Manager
```cpp
// src/recording/recording_manager.h
class RecordingManager {
public:
    void startRecording(const std::string& cameraId);
    void stopRecording(const std::string& cameraId);

    // Called when NAL unit received (passthrough, no decode needed)
    void onNalUnit(const std::string& cameraId, const NalUnit& nal);

    // Query recordings
    std::vector<RecordingSegment> getSegments(
        const std::string& cameraId,
        int64_t startTime,
        int64_t endTime
    );

private:
    std::unordered_map<std::string, std::unique_ptr<SegmentWriter>> writers_;
    Database database_;
    ThreadPool ioPool_{4, "RecordingIO"};
};
```

### Recording Architecture (Zero Re-encode)
```
RTSP Packet
    │
    ▼
RTP Depacketizer
    │
    ├──────────────────────┐
    │                      │
    ▼                      ▼
NAL Unit Queue         NAL Unit Queue
(for decode)           (for recording)
    │                      │
    ▼                      ▼
NVDEC Decoder         Segment Writer
    │                      │
    ▼                      ▼
GPU Frame              MKV File
    │                  (H264 passthrough)
    ▼
Display

** No CPU encoding on recording path! **
```

### Storage Format
```
storage/
├── camera_01/
│   └── 2026-01-26/
│       ├── 10-30-00.mkv
│       ├── 10-30-10.mkv
│       ├── 10-30-20.mkv
│       └── ...
├── camera_02/
│   └── ...
└── metadata.db (SQLite)
```

### Deliverables
- [ ] SegmentWriter with MKV muxing
- [ ] RecordingManager with async I/O
- [ ] SQLite database schema
- [ ] Segment querying API
- [ ] Storage cleanup (retention policy)
- [ ] Unit tests

### Validation
```
Test: Record 42 cameras for 1 hour
Expected:
- Zero re-encoding (verify CPU usage)
- Clean segment boundaries
- Correct metadata in database
- Storage: ~1.5GB/hour/camera at 1Mbps
```

---

## Phase 7: Playback System (Days 43-49)

### Objectives
- Timeline widget with thumbnails
- Seek functionality
- Multi-camera synchronized playback
- Export functionality

### Key Components

#### 7.1 Playback Controller
```cpp
// src/client/playback/playback_controller.h
class PlaybackController {
public:
    void setCamera(const std::string& cameraId);
    void setTimeRange(int64_t startTime, int64_t endTime);

    void play();
    void pause();
    void seek(int64_t timestamp);
    void setSpeed(float speed);  // 0.5x, 1x, 2x, 4x

    // For synchronized multi-camera playback
    void addSyncedCamera(const std::string& cameraId);
    void syncSeek(int64_t timestamp);

signals:
    void positionChanged(int64_t timestamp);
    void stateChanged(PlaybackState state);

private:
    std::vector<std::unique_ptr<FileDecoder>> decoders_;
    int64_t currentPosition_ = 0;
    PlaybackState state_ = PlaybackState::STOPPED;
};
```

#### 7.2 Timeline Widget
```cpp
// src/client/widgets/timeline_widget.h
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    void setTimeRange(int64_t startTime, int64_t endTime);
    void setAvailableSegments(const std::vector<TimeRange>& segments);
    void setPosition(int64_t timestamp);

signals:
    void seekRequested(int64_t timestamp);
    void rangeSelected(int64_t start, int64_t end);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    std::vector<TimeRange> segments_;
    int64_t currentPosition_ = 0;
};
```

### Deliverables
- [ ] PlaybackController with seek
- [ ] FileDecoder for MKV segments
- [ ] TimelineWidget with segment visualization
- [ ] Multi-camera sync playback
- [ ] Speed control (0.5x - 4x)
- [ ] Export to MP4/MKV

---

## Phase 8: Server Component & Multi-Client Support (Days 50-56)

### Objectives
- Headless server with multi-client broadcast architecture
- REST API for camera/user/recording management
- WebSocket for real-time video streaming and events
- Session management with per-client subscriptions
- KCMVP encrypted authentication (ECDH + ARIA)
- mDNS/Zeroconf server discovery
- Multi-server client with tab-based switching

### Key Components

#### 8.1 Enhanced Server API (REST + WebSocket)

```cpp
// src/server/api_server.h
class APIServer {
public:
    struct Config {
        std::string bindAddress = "0.0.0.0";
        int httpPort = 8080;
        int wsPort = 8081;
        bool enableTLS = false;
        std::string certPath;
        std::string keyPath;
        int maxClients = 16;
    };

    bool start(const Config& config);
    void stop();

private:
    SessionManager* sessionManager_;
    StreamBroadcaster* broadcaster_;
    Database* database_;
};
```

**REST Endpoints (HTTPS):**

```cpp
// Authentication
POST /api/auth/login             // Login, return session token
POST /api/auth/logout            // Logout
POST /api/auth/refresh           // Refresh session token

// Camera Management
GET  /api/cameras                // List all cameras (filtered by permissions)
GET  /api/cameras/{id}           // Get camera details
POST /api/cameras                // Add camera (admin only)
PUT  /api/cameras/{id}           // Update camera config (admin only)
DELETE /api/cameras/{id}         // Remove camera (admin only)
POST /api/cameras/{id}/start     // Start camera stream
POST /api/cameras/{id}/stop      // Stop camera stream
POST /api/cameras/{id}/ptz       // PTZ control (operator+)

// User Management (admin only)
GET  /api/users                  // List all users
POST /api/users                  // Create user
PUT  /api/users/{id}             // Update user permissions
DELETE /api/users/{id}           // Delete user

// Recording Management
GET  /api/recordings?camera={id}&start={ts}&end={ts}  // Query recordings
GET  /api/recordings/{id}/download                    // Download segment
POST /api/recordings/export                           // Export time range to MP4
DELETE /api/recordings/{id}                           // Delete segment (admin)

// System Management
GET  /api/system/info            // Server version, capabilities
GET  /api/system/stats           // CPU, RAM, GPU, bandwidth usage
GET  /api/system/sessions        // List active client sessions (admin)
DELETE /api/system/sessions/{id} // Kick client (admin)
GET  /api/system/logs            // Server logs (admin)

// Server Discovery (for multi-server clients)
GET  /api/discovery/info         // Server info for mDNS/discovery
```

**WebSocket Endpoints (WSS):**

```cpp
// Live streaming (primary use case)
WS  /ws/live                     // Live stream subscription

// Playback streaming
WS  /ws/playback                 // Historical playback

// Events/Notifications
WS  /ws/events                   // Real-time events (motion, alerts)
```

#### 8.2 Session Manager (Multi-Client Support)

```cpp
// src/server/session_manager.h
class SessionManager {
public:
    // Authentication (KCMVP ECDH + ARIA)
    struct AuthRequest {
        std::string username;
        std::string password;  // PBKDF2 hashed client-side
        std::vector<uint8_t> clientPublicKey;  // ECDH P-256
    };

    struct AuthResponse {
        bool success;
        std::string sessionId;
        std::string sessionToken;  // ARIA-256-CTR encrypted
        std::vector<uint8_t> serverPublicKey;  // ECDH P-256
        UserPermissions permissions;
        int64_t expiresAt;
    };

    AuthResponse authenticate(const AuthRequest& req, const std::string& clientIp);

    // Session management
    bool validateSession(const std::string& sessionToken);
    void updateActivity(const std::string& sessionId);
    void terminateSession(const std::string& sessionId);
    std::vector<ClientSession::Info> getActiveSessions() const;

    // Subscription management
    void subscribe(const std::string& sessionId,
                  const std::vector<std::string>& cameraIds,
                  const std::map<std::string, StreamQuality>& qualities);

    void unsubscribe(const std::string& sessionId,
                    const std::vector<std::string>& cameraIds);

    // Get clients subscribed to specific camera
    std::vector<ClientSession*> getSubscribedClients(const std::string& cameraId);

private:
    std::map<std::string, std::unique_ptr<ClientSession>> sessions_;
    std::shared_mutex mutex_;

    // ECDH key pairs (per session)
    std::map<std::string, std::vector<uint8_t>> sessionKeys_;

    // Rate limiting (prevent brute force)
    std::map<std::string, int> failedLoginAttempts_;
};
```

#### 8.3 Stream Broadcaster (Decode Once, Send Many)

```cpp
// src/server/stream_broadcaster.h
class StreamBroadcaster {
public:
    struct EncodedFrame {
        std::vector<uint8_t> data;
        int64_t pts;
        bool isKeyframe;
        StreamQuality quality;
    };

    // Called when frame decoded from camera
    void onFrameDecoded(const std::string& cameraId, const GPUFrame& frame);

    // Broadcast to all subscribed clients
    void broadcastFrame(const std::string& cameraId, const GPUFrame& frame);

private:
    // Encode once per quality level (not per client!)
    EncodedFrame encodeForQuality(const GPUFrame& frame, StreamQuality quality);

    // Send to specific client over WebSocket
    void sendToClient(ClientSession* client,
                     const std::string& cameraId,
                     const EncodedFrame& frame);

    // NVENC encoder pool (reuse encoders)
    std::map<StreamQuality, NvencEncoder*> encoders_;
    SessionManager* sessionManager_;
    ThreadPool encodePool_{4, "Encoding"};
};
```

#### 8.4 WebSocket Protocol (JSON Messages)

**Client → Server Messages:**

```json
// Subscribe to cameras
{
    "type": "subscribe",
    "sessionToken": "encrypted-token",
    "cameras": [
        {
            "id": "camera_01",
            "quality": "GRID_VIEW",
            "fps": 15
        },
        {
            "id": "camera_02",
            "quality": "FULLSCREEN",
            "fps": 30
        }
    ]
}

// Unsubscribe from cameras
{
    "type": "unsubscribe",
    "sessionToken": "encrypted-token",
    "cameras": ["camera_01", "camera_15"]
}

// PTZ control
{
    "type": "ptz",
    "sessionToken": "encrypted-token",
    "cameraId": "camera_05",
    "action": "move",
    "pan": 45,
    "tilt": -10,
    "zoom": 2.5
}

// Keepalive (every 30 seconds)
{
    "type": "ping",
    "sessionToken": "encrypted-token"
}
```

**Server → Client Messages:**

```json
// Frame delivery (binary WebSocket message)
{
    "type": "frame",
    "cameraId": "camera_01",
    "timestamp": 1738012345678,
    "format": "h264",
    "keyframe": false,
    "quality": "GRID_VIEW",
    "data": "<base64 H264 NAL units>"
}

// Camera event
{
    "type": "event",
    "cameraId": "camera_05",
    "event": "motion_detected",
    "timestamp": 1738012345678,
    "metadata": {
        "region": "zone_1",
        "confidence": 0.95
    }
}

// Session expired
{
    "type": "session_expired",
    "reason": "Token expired after 24 hours"
}

// Server status update
{
    "type": "server_status",
    "cpuUsage": 18,
    "gpuUsage": 35,
    "cameraCount": 42,
    "clientCount": 5
}

// Pong (response to ping)
{
    "type": "pong",
    "serverTime": 1738012345678
}
```

#### 8.5 Multi-Server Client (Tab-Based UI)

```cpp
// src/client/multi_server_manager.h
class MultiServerManager {
public:
    // Server management
    std::string addServer(const ServerConnection::Config& config);
    void removeServer(const std::string& serverId);
    void connectServer(const std::string& serverId);
    void disconnectServer(const std::string& serverId);

    // Get server list
    std::vector<ServerConnection*> getAllServers();
    ServerConnection* getServer(const std::string& serverId);

    // Active server (currently viewed tab)
    void setActiveServer(const std::string& serverId);
    ServerConnection* getActiveServer();

    // Auto-reconnect
    void enableAutoReconnect(bool enabled);

    // Server discovery
    void startDiscovery();
    std::vector<ServerDiscovery::DiscoveredServer> getDiscoveredServers();

signals:
    void serverAdded(const std::string& serverId);
    void serverRemoved(const std::string& serverId);
    void serverConnected(const std::string& serverId);
    void serverDisconnected(const std::string& serverId);
    void serverDiscovered(const ServerDiscovery::DiscoveredServer& server);

private:
    std::map<std::string, std::unique_ptr<ServerConnection>> servers_;
    std::string activeServerId_;
    ServerDiscovery discovery_;
    std::thread reconnectThread_;
};
```

```cpp
// src/client/widgets/multi_server_tab_widget.h
class MultiServerTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit MultiServerTabWidget(MultiServerManager* manager, QWidget* parent);

    // Tab management
    void addServerTab(const std::string& serverId, const std::string& name);
    void removeServerTab(const std::string& serverId);
    void updateTabStatus(const std::string& serverId, const std::string& status);

    // Tab indicators
    void setTabIcon(const std::string& serverId, ServerConnection::State state);
    // ✓ Connected (green), ⚠ Reconnecting (yellow), ✗ Offline (red)

protected:
    void currentChanged(int index) override;

private slots:
    void onServerTabChanged(int index);
    void onTabCloseRequested(int index);

private:
    MultiServerManager* manager_;
    std::map<std::string, ServerCameraGridWidget*> serverWidgets_;
};
```

#### 8.6 Server Discovery (mDNS/Zeroconf)

```cpp
// src/server/server_discovery_publisher.h
class ServerDiscoveryPublisher {
public:
    struct ServerInfo {
        std::string name;              // "Office HQ FluxVision"
        std::string version;           // "2.0.0"
        int cameraCount;
        int httpPort = 8080;
        int wsPort = 8081;
        std::string serverId;          // UUID
    };

    // Start publishing mDNS service
    // Service type: _fluxvision._tcp.local
    void startPublishing(const ServerInfo& info);
    void stopPublishing();

private:
    // Use Avahi (Linux) or Bonjour (cross-platform)
    void publishMDNS();
};
```

```cpp
// src/client/server_discovery.h
class ServerDiscovery {
public:
    struct DiscoveredServer {
        std::string name;
        std::string host;
        int httpPort;
        int wsPort;
        std::string version;
        int cameraCount;
        std::string serverId;
        bool isOnline;
    };

    void startBrowsing();
    void stopBrowsing();

    std::vector<DiscoveredServer> getDiscoveredServers() const;

signals:
    void serverDiscovered(const DiscoveredServer& server);
    void serverLost(const std::string& serverId);

private:
    void browseMDNS();  // Browse _fluxvision._tcp.local
    std::vector<DiscoveredServer> servers_;
};
```

#### 8.7 KCMVP Authentication Flow

```
┌──────────────────────────────────────────────────────────────────┐
│                Client-Server Authentication                      │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Client                                    Server                │
│    │                                          │                  │
│    ├─────► HTTPS POST /api/auth/login        │                  │
│    │       {                                  │                  │
│    │         "username": "operator1",         │                  │
│    │         "password_hash": "pbkdf2...",    │                  │
│    │         "client_public_key": "..."       │ ◄── ECDH P-256   │
│    │       }                                  │                  │
│    │                                          │                  │
│    │                    ┌─────────────────────┤                  │
│    │                    │ Verify credentials  │                  │
│    │                    │ (PBKDF2 + ARIA-GCM) │                  │
│    │                    └─────────────────────┤                  │
│    │                                          │                  │
│    │    ◄───── Response (200 OK) ─────────────┤                  │
│    │       {                                  │                  │
│    │         "success": true,                 │                  │
│    │         "session_id": "uuid",            │                  │
│    │         "session_token": "aria_enc...",  │ ◄── ARIA-256-CTR │
│    │         "server_public_key": "...",      │ ◄── ECDH P-256   │
│    │         "permissions": {...}             │                  │
│    │       }                                  │                  │
│    │                                          │                  │
│    ├─────────────────────────────────────────►│                  │
│    │        Derive Shared Secret (ECDH)       │                  │
│    │        Session Key = HKDF(shared_secret) │                  │
│    ├─────────────────────────────────────────►│                  │
│    │                                          │                  │
│    ├─────► WSS /ws/live                       │                  │
│    │       (Encrypted with session key)       │                  │
│    │                                          │                  │
│    │    ◄───── Encrypted Video Frames ────────┤                  │
│    │       (ARIA-256-CTR per frame)           │                  │
│    │                                          │                  │
└──────────────────────────────────────────────────────────────────┘
```

### Encryption Integration (KCMVP)

```cpp
// src/server/crypto/session_crypto.h
class SessionCrypto {
public:
    // ECDH key exchange
    struct KeyExchange {
        std::vector<uint8_t> clientPublicKey;
        std::vector<uint8_t> serverPublicKey;
        std::vector<uint8_t> sharedSecret;
    };

    // Generate ECDH P-256 key pair
    KeyExchange performKeyExchange(const std::vector<uint8_t>& clientPubKey);

    // Derive session key from shared secret (HKDF-SHA256)
    std::vector<uint8_t> deriveSessionKey(const std::vector<uint8_t>& sharedSecret);

    // Encrypt/decrypt WebSocket messages (ARIA-256-CTR)
    std::vector<uint8_t> encryptMessage(const std::vector<uint8_t>& plaintext,
                                       const std::vector<uint8_t>& sessionKey);

    std::vector<uint8_t> decryptMessage(const std::vector<uint8_t>& ciphertext,
                                       const std::vector<uint8_t>& sessionKey);

private:
    // Use OpenSSL EVP for ECDH
    // Use ARIA implementation from KryptoAPI or OpenSSL 3.0+
};
```

### Resource Management (Multi-Client)

```cpp
// src/server/resource_manager.h
class ClientResourceManager {
public:
    struct ClientQuota {
        int maxBandwidthMbps = 50;
        int maxCamerasFullscreen = 4;
        int maxCamerasTotal = 42;
        bool priorityClient = false;
    };

    // Enforce bandwidth limits
    bool canSendFrame(const std::string& sessionId,
                     const std::string& cameraId,
                     int frameSizeBytes);

    // Track bandwidth usage
    void recordFrameSent(const std::string& sessionId, int bytes);

    // Update quotas (admin function)
    void setClientQuota(const std::string& sessionId, const ClientQuota& quota);

    // Get bandwidth stats
    struct BandwidthStats {
        int totalClientsConnected;
        int totalBandwidthMbps;
        std::map<std::string, int> perClientBandwidthMbps;
    };

    BandwidthStats getStats() const;

private:
    std::map<std::string, ClientQuota> quotas_;
    std::map<std::string, int> currentUsageMbps_;
};
```

### Deliverables

#### Server-Side
- [ ] HTTP REST API server with TLS support
- [ ] WebSocket server for live streaming
- [ ] SessionManager with KCMVP authentication
- [ ] StreamBroadcaster with multi-client support
- [ ] NVENC encoder pool for multiple quality levels
- [ ] User/permissions database (SQLite)
- [ ] mDNS/Zeroconf server publishing
- [ ] Rate limiting and bandwidth management
- [ ] Session timeout and cleanup
- [ ] Admin panel API (user/camera/session management)

#### Client-Side
- [ ] Multi-server connection pool
- [ ] Tab-based server UI (QTabWidget)
- [ ] Server discovery browser
- [ ] Per-server camera grid
- [ ] Background server pause/resume
- [ ] Unified search across servers
- [ ] Server favorites/grouping
- [ ] Connection status indicators

### Validation

```
Test 1: Multi-Client Server (1 server, 8 clients)
- Connect 8 clients to one server
- Each client views different camera subsets
- Verify: decode happens once per camera
- Verify: bandwidth scales linearly with clients
- Expected: Server CPU <25%, each client receives correct cameras

Test 2: Multi-Server Client (1 client, 4 servers)
- Connect one client to 4 servers (10 cameras each)
- Switch between server tabs
- Verify: inactive tabs pause or reduce quality
- Expected: Client CPU <30%, smooth tab switching

Test 3: mDNS Discovery
- Start server on LAN
- Client discovers server automatically
- Verify: correct server info displayed
- Expected: <2 seconds discovery time

Test 4: Session Security
- Attempt replay attack with old session token
- Verify: server rejects expired/invalid tokens
- Expected: All unauthorized requests fail with 401

Test 5: Bandwidth Throttling
- Connect client with 25Mbps quota
- Subscribe to 20 cameras (60Mbps theoretical)
- Verify: server enforces quota (drops to lower quality)
- Expected: Client receives max 25Mbps, no crashes
```

---

## Phase 9: AI Integration Preparation (Days 57-63)

### Objectives
- Reserve GPU resources for AI
- Frame extraction API for inference
- Async inference pipeline
- Result overlay system

### Key Components

#### 9.1 AI Frame Provider
```cpp
// src/core/ai/frame_provider.h
class AIFrameProvider {
public:
    // Get latest frame for specific camera (for inference)
    bool getFrame(const std::string& cameraId, GPUFrame& frame);

    // Get frame at specific resolution (resize on GPU)
    bool getResizedFrame(const std::string& cameraId,
                         int width, int height,
                         GPUFrame& frame);

    // Batch frames for efficient inference
    bool getBatchFrames(const std::vector<std::string>& cameraIds,
                        std::vector<GPUFrame>& frames);
};
```

#### 9.2 Inference Result Handler
```cpp
// src/core/ai/inference_result.h
struct DetectionResult {
    std::string cameraId;
    int64_t timestamp;
    std::vector<BoundingBox> detections;
    float inferenceTime;
};

class InferenceResultHandler {
public:
    // Store result for overlay rendering
    void addResult(const DetectionResult& result);

    // Get latest result for camera
    std::optional<DetectionResult> getResult(const std::string& cameraId);
};
```

### AI Pipeline (Future)
```
GPUFrame (CUDA)
    │
    ▼
AIFrameProvider.getResizedFrame(640x640)
    │
    ▼
TensorRT / ONNX Runtime
    │
    ▼
Detection Results
    │
    ▼
Overlay Renderer
    │
    ▼
Final Display
```

### Resource Budget for AI
```
Total GPU Budget: 100%
├── Video Decode: 20%
├── Video Render: 15%
├── Reserved for AI: 50%
└── Headroom: 15%

Target AI Capabilities:
├── Object Detection: YOLO at 15fps on 8 cameras
├── Segmentation: On-demand (single camera)
└── LLM/VLM: Query-based (async)
```

### Deliverables
- [ ] AIFrameProvider API
- [ ] GPU memory reservation for AI
- [ ] Inference result handler
- [ ] Bounding box overlay renderer
- [ ] Example YOLO integration

---

## Phase 10: Testing & Optimization (Days 64-70)

### Test Suites

#### 10.1 Unit Tests
```
- Decoder tests (H264, H265)
- Network tests (RTSP connect, reconnect)
- Threading tests (queue, pool)
- Recording tests (segment write, query)
```

#### 10.2 Integration Tests
```
- Single camera end-to-end
- 10 camera stability (1 hour)
- 42 camera stress test (4 hours)
- Playback accuracy test
```

#### 10.3 Performance Tests
```
- Baseline comparison with v1
- Resource usage profiling
- Latency measurement
- Memory leak detection (Valgrind/ASan)
```

### Optimization Targets
```
If CPU > 25%:
├── Profile decode thread
├── Check for unnecessary copies
└── Verify hardware decode active

If GPU > 50%:
├── Check texture upload efficiency
├── Verify NV12 shader
└── Profile render calls

If RAM > 1GB:
├── Check queue sizes
├── Profile memory allocations
└── Verify surface pool

If Latency > 100ms:
├── Check queue depths
├── Profile network receive
└── Verify no software decode
```

### Deliverables
- [ ] Test suite (>80% coverage)
- [ ] CI pipeline
- [ ] Performance regression tests
- [ ] Memory leak verification
- [ ] Documentation

---

## Summary Timeline

```
Phase 0: Foundation Setup        Days 1-2    (2 days)
Phase 1: Core Decoder            Days 3-7    (5 days)
Phase 2: Network Layer           Days 8-14   (7 days)
Phase 3: Threading & Memory      Days 15-21  (7 days)
Phase 4: GPU Rendering           Days 22-28  (7 days)
Phase 5: Qt UI Integration       Days 29-35  (7 days)
Phase 6: Recording Engine        Days 36-42  (7 days)
Phase 7: Playback System         Days 43-49  (7 days)
Phase 8: Server Component        Days 50-56  (7 days)
Phase 9: AI Preparation          Days 57-63  (7 days)
Phase 10: Testing & Polish       Days 64-70  (7 days)
─────────────────────────────────────────────────────
Total:                           70 days (~14 weeks)
```

### Milestone Checkpoints

| Milestone | Phase | Success Criteria |
|-----------|-------|------------------|
| M1: Single Camera | 1-2 | One camera decoding with NVDEC, <3% CPU |
| M2: Multi-Camera | 3 | 42 cameras decoding, <20% CPU |
| M3: Live View | 4-5 | Full UI with smooth rendering |
| M4: Recording | 6 | Zero re-encode recording working |
| M5: Playback | 7 | Timeline with seek functional |
| M6: Production | 8-10 | Full feature parity with v1 |

---

## Quick Start Commands

```bash
# Phase 0: Setup
cd /home/shokhrukh/nflux/docker/nvr/VMS
mkdir -p src/{core/{codec,network,gpu,threading,stream},recording,client/{widgets,rendering},common}
mkdir -p tests tools config docs cmake

# Verify NVDEC
nvidia-smi
ffmpeg -decoders | grep nvdec

# Build (after CMake setup)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Dependencies

### Required
```
- CUDA Toolkit 12.0+
- NVIDIA Video Codec SDK 12.0+
- Qt5 (Widgets, OpenGL)
- FFmpeg (libavformat, libavcodec)
- SQLite3
- yaml-cpp
```

### Optional
```
- VA-API (Intel/AMD support)
- TensorRT (AI inference)
- OpenCV (image processing)
```

---

## Success Metrics

After full implementation:

| Metric | Target |
|--------|--------|
| Camera Count | 42+ simultaneous |
| CPU Usage | <20% |
| GPU Usage | <40% |
| RAM Usage | <800MB |
| VRAM Usage | <4GB |
| Latency | <100ms |
| Thread Count | <25 |
| AI Headroom | 50%+ GPU available |

This architecture will match or exceed NX Witness performance while providing a foundation for AI-powered analytics.
