// densf.cpp: Extracts .sa files from a Scientific Atlanta formatted Sega Channel
// game image file.
// Author: Nathan Misner
// I place this file in the public domain.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

#define NUM_PIPES 10
#define PACKET_LEN 288
#define PACKET_DATA_LEN 246

uint8_t pipes[NUM_PIPES][PACKET_LEN];

#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
    int base;
    int len;
    uint8_t data[4 * 1024 * 1024];
} GameFile;
GameFile newFile;

std::map<uint16_t, GameFile> gameFiles;

static const uint8_t reverseByteLut[] = {
    0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
    0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
    0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
    0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
    0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
    0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
    0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
    0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
    0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
    0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
    0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
    0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
    0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
    0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
    0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
    0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
    0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
    0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
    0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
    0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
    0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
    0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
    0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
    0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
    0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
    0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
    0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
    0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
    0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
    0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
    0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
    0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};

void OrBits(uint8_t *source, unsigned sbitoff, uint8_t *dest, int numbit) {
    sbitoff--;
    unsigned sbytes = sbitoff >> 3;
    sbitoff &= 7;

    unsigned dbitoff = 0;
    unsigned dbytes = 0;

    while (numbit) {
        if (sbitoff == 8) {
            sbytes++;
            sbitoff = 0;
        }
        if (dbitoff == 8) {
            dbytes++;
            dbitoff = 0;
        }
        if (source[sbytes] & (1 << sbitoff)) {
            dest[dbytes] |= (1 << dbitoff);
        }
        sbitoff++;
        dbitoff++;
        numbit--;
    }
}

uint8_t RevBits8(uint8_t source, int bits) {
    return reverseByteLut[source] >> (8 - bits);
}

uint16_t RevBits16(uint16_t source, int bits) {
    uint8_t lowReversed = reverseByteLut[source & 0xff];
    uint8_t highReversed = reverseByteLut[source >> 8];
    return ((lowReversed << 8) | highReversed) >> (16 - bits);
}

void DeWeave(uint8_t *data) {
    unsigned cursor = 0;
    for (unsigned i = 0; i < PACKET_LEN; i += 2) {
        for (unsigned j = 0; j < NUM_PIPES; j++) {
            pipes[j][i] = data[cursor++];
            pipes[j][i + 1] = data[cursor++];
        }
    }
}

void DeInterLeave(uint8_t *data) {
    uint8_t interFrame[PACKET_LEN] = { 0 };
    unsigned packetAoff;
    unsigned packetBoff;
    unsigned packetXoff;
    unsigned packetAbit;
    unsigned packetBbit;
    unsigned bitCount;
    unsigned pc;
    unsigned packet;

    OrBits(data, 1, interFrame, 27);
    packetAoff = 0;
    packetBoff = 225;
    packetXoff = packetAoff;
    packet = 0;
    while (packet < 9) {
        if (packet == 0) {
            packetAoff += 27;
            packetBoff += 27;
            packetXoff += 27;
        }
        if (packet == 4) {
            packetBoff += 27;
        }
        if (packet == 6) {
            packetAoff += 27;
            packetXoff += 27;
        }

        bitCount = 0;
        pc = 0;
        unsigned *bitPtr = &packetAbit;
        packetAbit = 0;
        packetBbit = 0;
        while (bitCount < 450) {
            if (pc == 2) {
                pc = 0;
                if (bitPtr == &packetAbit) { bitPtr = &packetBbit; }
                else { bitPtr = &packetAbit; }
            }

            if (bitCount == 225) {
                bitPtr = &packetBbit;
                pc = 0;
            }

            unsigned packetBits;
            if (bitPtr == &packetAbit) {
                packetBits = *bitPtr + packetAoff;
            }
            else {
                packetBits = *bitPtr + packetBoff;
            }

            if ((packet == 4) && (bitCount == 225)) {
                packetXoff += 27;
            }

            // copy bit #sourceBits to bit packetBits
            unsigned sourceBits = bitCount + packetXoff;
            if (data[sourceBits >> 3] & (1 << (sourceBits & 7))) {
                interFrame[packetBits >> 3] |= (1 << (packetBits & 7));
            }
            (*bitPtr)++;
            bitCount++;
            pc++;
        }

        packetAoff += 450;
        packetBoff += 450;
        packetXoff = packetAoff;
        packet += 2;
    }

    memcpy(data, interFrame, PACKET_LEN);
}

void GetData(uint8_t *in, uint8_t *out) {
    uint8_t rData[PACKET_DATA_LEN] = { 0 };

    int bitoff = 140;
    for (int i = 0; i < PACKET_DATA_LEN;) {
        if (bitoff == 1153) {
            bitoff += 27;
        }

        if (i == 0) {
            OrBits(in, bitoff, rData, 96);
            bitoff += 96;
            i += 12;
        }
        else {
            OrBits(in, bitoff, rData + i, 208);
            bitoff += 208;
            i += 26;
        }
        // skip bch & parity
        bitoff += 17;
    }

    for (int i = 0; i < PACKET_DATA_LEN; i++) {
        out[i] = reverseByteLut[rData[i]];
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("use: densf file.img outdir");
        return -1;
    }

    // load the image file into ram
    FILE *infile = fopen(argv[1], "rb");
    if (!infile) {
        printf("couldn't open %s\n", argv[1]);
        return -2;
    }
    fseek(infile, 0, SEEK_END);
    size_t fileSize = ftell(infile);
    rewind(infile);
    if (fileSize % (PACKET_LEN * NUM_PIPES)) {
        printf("%s: invalid file\n", argv[1]);
        fclose(infile);
        return -3;
    }
    // never freed because this is a one-shot program
    uint8_t *fileData = (uint8_t *)malloc(fileSize);
    if (!fileData) { abort(); }
    fread(fileData, 1, fileSize, infile);
    fclose(infile);

    // decode the file data from the packets in the image file
    size_t fileCursor = 0;
    while (fileCursor < fileSize) {
        DeWeave(fileData + fileCursor);
        fileCursor += (PACKET_LEN * NUM_PIPES);

        uint8_t serviceId;
        uint16_t fileId;
        uint16_t address;
        for (int i = 0; i < NUM_PIPES; i++) {
            DeInterLeave(pipes[i]);
            serviceId = 0;
            OrBits(pipes[i], 32, &serviceId, 7);
            serviceId = RevBits8(serviceId, 7);
            fileId = 0;
            OrBits(pipes[i], 39, (uint8_t *)&fileId, 14);
            fileId = RevBits16(fileId, 14);
            address = 0;
            OrBits(pipes[i], 53, (uint8_t *)&address, 15);
            address = RevBits16(address, 15);

            if (fileId != 0x3fff) {
                if (!gameFiles.count(fileId)) {
                    printf("found new file: %u sid: %u\n", fileId, serviceId);
                    memset(&newFile, 0, sizeof(newFile));
                    newFile.base = address;
                    newFile.len = 0;
                    gameFiles[fileId] = newFile;
                }
                GameFile &file = gameFiles[fileId];
                int offset = (address - file.base) * PACKET_DATA_LEN;
                if (offset < 0) {
                    printf("\n%d: negative offset!\n", fileId);
                }
                else {
                    GetData(pipes[i], file.data + offset);
                    if ((offset + PACKET_DATA_LEN) > file.len) {
                        file.len = offset + PACKET_DATA_LEN;
                    }
                }
            }
        }
    }

    // write out the decoded files to disk
    std::string outDir = std::string(argv[2]);
    std::filesystem::create_directory(outDir);
    std::string filename;
    for (auto const &game : gameFiles) {
        filename = std::format("{}/{}.sa", outDir, game.first);
        FILE *outfile = fopen(filename.c_str(), "wb");
        fwrite(game.second.data, 1, game.second.len, outfile);
        fclose(outfile);
    }

    return 0;
}
